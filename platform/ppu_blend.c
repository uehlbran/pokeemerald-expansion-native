/*
 * ppu_blend.c: the LCD controller's colour special effects.
 *
 * Stands in for the last stage of the GBA's line composition, GBATEK "LCD I/O
 * Color Special Effects"
 * (https://problemkaputt.de/gbatek.htm#lcdiocolorspecialeffects): BLDCNT picks
 * 1st and 2nd target layers and an effect, alpha blending mixes the top-most
 * pixel (a 1st target) with the pixel directly under it (if a 2nd target)
 * using BLDALPHA's EVA and EVB, brightness increase or decrease moves a 1st
 * target towards white or black by BLDY's EVY. Semi-transparent OBJs are 1st
 * targets and alpha-blend regardless of BLDCNT, when a 2nd target is under
 * them. The window's effect bit (ppu_window.c) can switch all of it off per
 * dot. Clean room from GBATEK; the reference ports did not clamp EVA, EVB and
 * EVY to 16, which this does.
 *
 * Interpretation: a semi-transparent OBJ with no 2nd target under it gets
 * BLDCNT's brightness effect only if BLDCNT selects OBJ as a 1st target
 * (GBATEK: "The BLDCNT register may be used to perform Brightness effects on
 * the OBJ"; mGBA does the same). The window's effect bit disables
 * semi-transparency too (mGBA does the same; GBATEK is silent).
 */
#include "ppu_internal.h"

/* GBATEK "LCD I/O Color Special Effects", BLDCNT. */
#define BLDCNT_SECOND_TARGET_SHIFT 8u /* bits 8-13: the 2nd target layers */
#define BLDCNT_EFFECT_SHIFT 6u        /* bits 6-7: the effect */
#define BLDCNT_EFFECT_MASK 0x3u
enum blend_effect { EFFECT_NONE = 0, EFFECT_ALPHA = 1, EFFECT_BRIGHTEN = 2, EFFECT_DARKEN = 3 };

/* BLDALPHA bits 0-4 EVA, 8-12 EVB; BLDY bits 0-4 EVY: "0..16 = 0/16..16/16,
 * 17..31=16/16". */
#define COEFFICIENT_MASK 0x1Fu
#define BLDALPHA_EVB_SHIFT 8u
#define COEFFICIENT_MAX 16u
#define COEFFICIENT_SHIFT 4u /* the coefficients are sixteenths */

/* GBATEK "LCD Color Palettes": 5 bits each of red (0-4), green (5-9), blue (10-14). */
#define CHANNEL_BITS 5u
#define CHANNEL_MAX 31u
#define CHANNEL_COUNT 3u

static uint32_t coefficient(uint32_t field)
{
    const uint32_t value = field & COEFFICIENT_MASK;
    return value > COEFFICIENT_MAX ? COEFFICIENT_MAX : value;
}

static uint32_t channel(uint16_t colour, unsigned index)
{
    return ((uint32_t)colour >> (index * CHANNEL_BITS)) & CHANNEL_MAX;
}

/* colour with channel index set to value (0-31), the channel being 0 before. */
static uint16_t with_channel(uint16_t colour, unsigned index, uint32_t value)
{
    return (uint16_t)(colour | value << (index * CHANNEL_BITS));
}

uint16_t ppu_blend_alpha(uint16_t first, uint16_t second, uint16_t bldalpha)
{
    const uint32_t eva = coefficient(bldalpha);
    const uint32_t evb = coefficient((uint32_t)bldalpha >> BLDALPHA_EVB_SHIFT);
    uint16_t result = 0;
    for (unsigned i = 0; i < CHANNEL_COUNT; i++) {
        const uint32_t mixed = (channel(first, i) * eva + channel(second, i) * evb) >> COEFFICIENT_SHIFT;
        result = with_channel(result, i, mixed > CHANNEL_MAX ? CHANNEL_MAX : mixed);
    }
    return result;
}

uint16_t ppu_blend_brighten(uint16_t colour, uint16_t bldy)
{
    const uint32_t evy = coefficient(bldy);
    uint16_t result = 0;
    for (unsigned i = 0; i < CHANNEL_COUNT; i++) {
        const uint32_t value = channel(colour, i);
        result = with_channel(result, i, value + (((CHANNEL_MAX - value) * evy) >> COEFFICIENT_SHIFT));
    }
    return result;
}

uint16_t ppu_blend_darken(uint16_t colour, uint16_t bldy)
{
    const uint32_t evy = coefficient(bldy);
    uint16_t result = 0;
    for (unsigned i = 0; i < CHANNEL_COUNT; i++) {
        const uint32_t value = channel(colour, i);
        result = with_channel(result, i, value - ((value * evy) >> COEFFICIENT_SHIFT));
    }
    return result;
}

static bool is_first_target(uint16_t bldcnt, enum ppu_layer layer)
{
    return layer <= PPU_LAYER_BACKDROP && (((uint32_t)bldcnt >> layer) & 1u) != 0;
}

static bool is_second_target(uint16_t bldcnt, enum ppu_layer layer)
{
    return layer <= PPU_LAYER_BACKDROP && (((uint32_t)bldcnt >> (BLDCNT_SECOND_TARGET_SHIFT + layer)) & 1u) != 0;
}

uint16_t ppu_blend_pixel(const struct ppu_regs *regs, const struct ppu_pixel_targets *targets, bool effects_enabled)
{
    const struct ppu_target top = targets->top;
    if (!effects_enabled)
        return top.colour;
    const bool below_is_second = is_second_target(regs->bldcnt, targets->below.layer);
    if (targets->top_is_semi_transparent && below_is_second)
        return ppu_blend_alpha(top.colour, targets->below.colour, regs->bldalpha);
    if (!is_first_target(regs->bldcnt, top.layer))
        return top.colour;
    switch ((regs->bldcnt >> BLDCNT_EFFECT_SHIFT) & BLDCNT_EFFECT_MASK) {
    case EFFECT_ALPHA:
        return below_is_second ? ppu_blend_alpha(top.colour, targets->below.colour, regs->bldalpha) : top.colour;
    case EFFECT_BRIGHTEN:
        return ppu_blend_brighten(top.colour, regs->bldy);
    case EFFECT_DARKEN:
        return ppu_blend_darken(top.colour, regs->bldy);
    default: /* EFFECT_NONE */
        return top.colour;
    }
}
