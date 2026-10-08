/*
 * ppu.c: the LCD controller's drawing, one line at a time: the orchestration
 * and the composition of the layers.
 *
 * Stands in for the GBA's LCD video controller as GBATEK "LCD Video
 * Controller" describes it
 * (https://problemkaputt.de/gbatek.htm#gbalcdvideocontroller): for each
 * visible line it reads the LCD I/O registers as they are at that moment,
 * draws the enabled backgrounds (ppu_bg.c) and OBJs (ppu_obj.c), works out
 * the window region of each dot (ppu_window.c), finds the top-most and the
 * next visible pixel of each dot by priority and applies the colour special
 * effects (ppu_blend.c). It also owns the BG2/BG3 internal reference points,
 * latched at VBlank and stepped after each line. What it does not do, and
 * why: ppu.h. Clean room from GBATEK; the reference ports' renderers were not
 * read.
 *
 * PRIORITY (GBATEK "LCD I/O BG Control", "LCD OBJ - OAM Attributes"): a lower
 * priority value is in front; among BGs of equal priority the lower-numbered
 * BG is in front; an OBJ is in front of a BG of the same priority. The OBJ
 * layer offers one pixel per dot (ppu_obj.c), so OBJs never blend with each
 * other. The backdrop, BG palette colour 0, is behind everything.
 *
 * THE REFERENCE POINTS (GBATEK "LCD I/O BG Rotation/Scaling", Internal
 * Reference Point Registers): copied from BGxX/BGxY by ppu_latch_vblank(),
 * re-copied when the registers change before a line (a write, see
 * ppu_affine_reference_pick_up_writes), stepped by PB/PD after every line.
 * Interpretation: they step after every line, in any mode, during forced
 * blank and with the BG off, as GBATEK states it without conditions (mGBA
 * steps only an enabled BG in modes 1-5 outside forced blank). The game
 * latches anew every VBlank, so this can differ only within a frame in which
 * BG2 is switched on or forced blank ends mid-frame.
 *
 * State: the module's only state is `ppu` below (whether it is initialised,
 * and the two reference points). Every line buffer is on the stack.
 */
#include "ppu.h"
#include "ppu_internal.h"

/* GBATEK "GBA I/O Map", LCD I/O Registers: offsets from 04000000. */
#define REG_DISPCNT 0x000u
#define REG_BG0CNT 0x008u /* BGnCNT at + 2n */
#define REG_BGCNT_STRIDE 2u
#define REG_BG0HOFS 0x010u /* BGnHOFS at + 4n, BGnVOFS two bytes after it */
#define REG_BG0VOFS 0x012u
#define REG_BG_SCROLL_STRIDE 4u
#define REG_BG2PA 0x020u /* BG2PA, PB, PC, PD, then BG2X and BG2Y; BG3's at + 0x10 */
#define REG_AFFINE_PB 0x2u
#define REG_AFFINE_PC 0x4u
#define REG_AFFINE_PD 0x6u
#define REG_AFFINE_X 0x8u
#define REG_AFFINE_Y 0xCu
#define REG_AFFINE_STRIDE 0x10u
#define REG_WIN0H 0x040u /* WIN1H two bytes after */
#define REG_WIN0V 0x044u /* WIN1V two bytes after */
#define REG_WINDOW_STRIDE 2u
#define REG_WININ 0x048u
#define REG_WINOUT 0x04Au
#define REG_MOSAIC 0x04Cu
#define REG_BLDCNT 0x050u
#define REG_BLDALPHA 0x052u
#define REG_BLDY 0x054u
#define REG_HIGH_HALF 2u /* a 32-bit register's upper halfword */
#define HALFWORD_BITS 16u

enum bg_mode { MODE_0 = 0, MODE_1 = 1 }; /* the modes drawn; 2-7 are not */

/* GBATEK "LCD I/O BG Control", BGxCNT bits 0-1. */
#define BGCNT_PRIORITY_MASK 0x3u

/* GBATEK "LCD I/O Display Control", Blanking Bits: forced blank "display[s]
 * white lines". An unsupported mode is drawn black. */
#define FORCED_BLANK_COLOUR 0x7FFFu
#define UNSUPPORTED_MODE_COLOUR 0x0000u

static struct {
    bool initialized;
    struct ppu_affine_reference reference[PPU_AFFINE_BG_COUNT]; /* BG2, BG3 */
} ppu;

/* ---- the registers ---- */

static uint32_t io_word(uint32_t offset)
{
    return (uint32_t)ppu_io_halfword(offset)
           | (uint32_t)ppu_io_halfword(offset + REG_HIGH_HALF) << HALFWORD_BITS;
}

static void read_regs(struct ppu_regs *regs)
{
    regs->dispcnt = ppu_io_halfword(REG_DISPCNT);
    for (uint32_t bg = 0; bg < PPU_BG_COUNT; bg++) {
        regs->bgcnt[bg] = ppu_io_halfword(REG_BG0CNT + bg * REG_BGCNT_STRIDE);
        regs->hofs[bg] = ppu_io_halfword(REG_BG0HOFS + bg * REG_BG_SCROLL_STRIDE);
        regs->vofs[bg] = ppu_io_halfword(REG_BG0VOFS + bg * REG_BG_SCROLL_STRIDE);
    }
    for (uint32_t i = 0; i < PPU_AFFINE_BG_COUNT; i++) {
        const uint32_t base = REG_BG2PA + i * REG_AFFINE_STRIDE;
        struct ppu_affine_regs *affine = &regs->affine[i];
        affine->pa = ppu_signed_halfword(ppu_io_halfword(base));
        affine->pb = ppu_signed_halfword(ppu_io_halfword(base + REG_AFFINE_PB));
        affine->pc = ppu_signed_halfword(ppu_io_halfword(base + REG_AFFINE_PC));
        affine->pd = ppu_signed_halfword(ppu_io_halfword(base + REG_AFFINE_PD));
        affine->x = io_word(base + REG_AFFINE_X);
        affine->y = io_word(base + REG_AFFINE_Y);
    }
    for (uint32_t w = 0; w < PPU_WINDOW_COUNT; w++) {
        regs->winh[w] = ppu_io_halfword(REG_WIN0H + w * REG_WINDOW_STRIDE);
        regs->winv[w] = ppu_io_halfword(REG_WIN0V + w * REG_WINDOW_STRIDE);
    }
    regs->winin = ppu_io_halfword(REG_WININ);
    regs->winout = ppu_io_halfword(REG_WINOUT);
    regs->mosaic = ppu_io_halfword(REG_MOSAIC);
    regs->bldcnt = ppu_io_halfword(REG_BLDCNT);
    regs->bldalpha = ppu_io_halfword(REG_BLDALPHA);
    regs->bldy = ppu_io_halfword(REG_BLDY);
}

/* ---- the layers of a line ---- */

struct line_layers {
    uint16_t bg_dots[PPU_BG_COUNT][PPU_SCREEN_WIDTH];
    unsigned bg_priority[PPU_BG_COUNT];
    unsigned order[PPU_BG_COUNT]; /* the drawn BGs, front to back */
    unsigned bg_count;
    struct ppu_obj_line obj;
    uint8_t window[PPU_SCREEN_WIDTH];
};

/* GBATEK "LCD I/O Display Control", the mode table: mode 0 has four text
 * BGs, mode 1 text BG0 and BG1 and rotation/scaling BG2 ("012-"). */
#define MODE_1_BG_COUNT 3u

static bool bg_in_mode(unsigned bg, enum bg_mode mode)
{
    return mode == MODE_0 || bg < MODE_1_BG_COUNT;
}

static void draw_bg(const struct ppu_regs *regs, enum bg_mode mode, unsigned bg, uint16_t line,
                    uint16_t dots[PPU_SCREEN_WIDTH])
{
    if (mode == MODE_1 && bg >= PPU_AFFINE_BG_FIRST)
        ppu_bg_render_affine(regs, bg, &ppu.reference[bg - PPU_AFFINE_BG_FIRST], line, dots);
    else
        ppu_bg_render_text(regs, bg, line, dots);
}

/* Draws the enabled BGs and lists them front to back: by priority, then by
 * BG number (the insertion keeps equal priorities in BG order). */
static void draw_bgs(const struct ppu_regs *regs, enum bg_mode mode, uint16_t line,
                     struct line_layers *layers)
{
    layers->bg_count = 0;
    for (unsigned bg = 0; bg < PPU_BG_COUNT; bg++) {
        if (!(regs->dispcnt & (PPU_DISPCNT_BG0_ON << bg)) || !bg_in_mode(bg, mode))
            continue;
        draw_bg(regs, mode, bg, line, layers->bg_dots[bg]);
        const unsigned priority = regs->bgcnt[bg] & BGCNT_PRIORITY_MASK;
        layers->bg_priority[bg] = priority;
        unsigned slot = layers->bg_count++;
        for (; slot > 0 && layers->bg_priority[layers->order[slot - 1]] > priority; slot--)
            layers->order[slot] = layers->order[slot - 1];
        layers->order[slot] = bg;
    }
}

/* ---- composition ---- */

/* GBATEK "LCD I/O Color Special Effects": effects look at the top-most
 * pixel (1st target) and the one directly under it (2nd target). */
#define DOT_TARGETS 2u

/* The first two visible pixels of a dot, front to back. */
struct dot_stack {
    struct ppu_target pixel[DOT_TARGETS];
    unsigned count;
};

static void stack_push(struct dot_stack *stack, uint16_t colour, enum ppu_layer layer)
{
    if (stack->count < DOT_TARGETS)
        stack->pixel[stack->count++] = (struct ppu_target){ colour, layer };
}

static struct ppu_pixel_targets dot_targets(const struct line_layers *layers, unsigned x,
                                            uint16_t backdrop)
{
    const uint8_t window = layers->window[x];
    const struct ppu_obj_line *obj = &layers->obj;
    bool obj_pending = (window & (1u << PPU_LAYER_OBJ)) && obj->colour[x] != PPU_TRANSPARENT;
    struct dot_stack stack = { .count = 0 };
    for (unsigned i = 0; i < layers->bg_count && stack.count < DOT_TARGETS; i++) {
        const unsigned bg = layers->order[i];
        if (obj_pending && obj->priority[x] <= layers->bg_priority[bg]) {
            stack_push(&stack, obj->colour[x], PPU_LAYER_OBJ);
            obj_pending = false;
        }
        const uint16_t dot = layers->bg_dots[bg][x];
        if ((window & (1u << bg)) && dot != PPU_TRANSPARENT)
            stack_push(&stack, dot, (enum ppu_layer)bg);
    }
    if (obj_pending)
        stack_push(&stack, obj->colour[x], PPU_LAYER_OBJ);
    stack_push(&stack, backdrop, PPU_LAYER_BACKDROP);
    stack_push(&stack, 0, PPU_LAYER_NONE);
    return (struct ppu_pixel_targets){
        .top = stack.pixel[0],
        .below = stack.pixel[1],
        .top_is_semi_transparent = stack.pixel[0].layer == PPU_LAYER_OBJ && obj->semi_transparent[x],
    };
}

static void fill(uint16_t out[PPU_SCREEN_WIDTH], uint16_t colour)
{
    for (unsigned x = 0; x < PPU_SCREEN_WIDTH; x++)
        out[x] = colour;
}

static int draw_line(const struct ppu_regs *regs, uint16_t line, uint16_t out[PPU_SCREEN_WIDTH])
{
    if (regs->dispcnt & PPU_DISPCNT_FORCED_BLANK) {
        fill(out, FORCED_BLANK_COLOUR);
        return PPU_OK;
    }
    const unsigned mode = regs->dispcnt & PPU_DISPCNT_MODE_MASK;
    if (mode != MODE_0 && mode != MODE_1) {
        fill(out, UNSUPPORTED_MODE_COLOUR);
        return PPU_ERR_UNSUPPORTED_MODE;
    }
    struct line_layers layers;
    draw_bgs(regs, (enum bg_mode)mode, line, &layers);
    if (regs->dispcnt & PPU_DISPCNT_OBJ_ON)
        ppu_obj_render_line(regs, line, &layers.obj);
    else
        ppu_obj_clear_line(&layers.obj);
    ppu_window_render_line(regs, line, &layers.obj, layers.window);
    const uint16_t backdrop = ppu_palette_colour(PPU_BACKDROP_COLOUR_INDEX);
    for (unsigned x = 0; x < PPU_SCREEN_WIDTH; x++) {
        const struct ppu_pixel_targets targets = dot_targets(&layers, x, backdrop);
        out[x] = ppu_blend_pixel(regs, &targets, (layers.window[x] & PPU_WINDOW_EFFECTS_ENABLE) != 0);
    }
    return PPU_OK;
}

/* ---- the interface ---- */

int ppu_init(void)
{
    ppu.initialized = true;
    ppu_latch_vblank();
    return PPU_OK;
}

void ppu_shutdown(void)
{
    ppu.initialized = false;
}

void ppu_latch_vblank(void)
{
    struct ppu_regs regs;
    read_regs(&regs);
    for (unsigned i = 0; i < PPU_AFFINE_BG_COUNT; i++)
        ppu_affine_reference_latch(&ppu.reference[i], &regs.affine[i]);
}

int ppu_render_line(uint16_t line, uint16_t *out)
{
    if (!ppu.initialized)
        return PPU_ERR_NOT_INITIALIZED;
    if (out == NULL)
        return PPU_ERR_NULL_OUTPUT;
    if (line >= PPU_SCREEN_HEIGHT)
        return PPU_ERR_BAD_LINE;
    struct ppu_regs regs;
    read_regs(&regs);
    for (unsigned i = 0; i < PPU_AFFINE_BG_COUNT; i++)
        ppu_affine_reference_pick_up_writes(&ppu.reference[i], &regs.affine[i]);
    const int status = draw_line(&regs, line, out);
    /* GBATEK: the internal registers "are then incremented by dmx and dmy
     * after each scanline". */
    for (unsigned i = 0; i < PPU_AFFINE_BG_COUNT; i++)
        ppu_affine_reference_step(&ppu.reference[i], &regs.affine[i]);
    return status;
}
