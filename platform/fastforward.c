/*
 * fastforward.c: fast-forward's keys, state and indicator (fastforward.h).
 *
 * Written from the behaviour fastforward.h fixes; SDL's key names are looked
 * up through input.h, which owns the keyboard. No emulator's code is
 * followed: mGBA and VisualBoyAdvance are where the keys and the "plays sped
 * up" sound come from, as conventions.
 */
#include "fastforward.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <SDL_events.h>

#include "gba_timing.h"
#include "input.h"
#include "overlay.h"

#define MODULE "fastforward"

#define MODIFIER_SEPARATOR '+'
#define UNLIMITED_INDICATOR "FF max"

/* A modifier: its name in the settings file and on the screen, and SDL's
 * state bits for it, either side. In the order the names are shown. */
struct modifier {
    unsigned flag;
    const char *setting;
    const char *name;
    uint16_t sdl_mask;
};

static const struct modifier modifiers[] = {
    {FASTFORWARD_CTRL, "ctrl", "Ctrl", KMOD_CTRL},
    {FASTFORWARD_ALT, "alt", "Alt", KMOD_ALT},
    {FASTFORWARD_SHIFT, "shift", "Shift", KMOD_SHIFT},
};
#define MODIFIER_COUNT (sizeof modifiers / sizeof modifiers[0])

/* The modifier keys, each with the state bit SDL sets while it is down. */
static const struct {
    int scancode;
    uint16_t sdl_bit;
} modifier_keys[] = {
    {SDL_SCANCODE_LCTRL, KMOD_LCTRL}, {SDL_SCANCODE_RCTRL, KMOD_RCTRL},   {SDL_SCANCODE_LALT, KMOD_LALT},
    {SDL_SCANCODE_RALT, KMOD_RALT},   {SDL_SCANCODE_LSHIFT, KMOD_LSHIFT}, {SDL_SCANCODE_RSHIFT, KMOD_RSHIFT},
};
#define MODIFIER_KEY_COUNT (sizeof modifier_keys / sizeof modifier_keys[0])

static struct {
    bool enabled;
    bool held;
    bool toggled;
    struct fastforward_settings settings;
} fastforward;

struct fastforward_settings fastforward_defaults(void)
{
    return (struct fastforward_settings){
        .hold = {.scancode = SDL_SCANCODE_TAB, .modifiers = 0u},
        .toggle = {.scancode = SDL_SCANCODE_TAB, .modifiers = FASTFORWARD_SHIFT},
        .speed = FASTFORWARD_DEFAULT_SPEED,
        .audio = FASTFORWARD_AUDIO_PLAY,
    };
}

static const char *skip_blanks(const char *text)
{
    while (*text == ' ' || *text == '\t')
        text++;
    return text;
}

/* If text starts with a modifier's name, blanks and a '+', sets *flag and
 * returns what follows the '+'; else NULL. tolower is the C locale's, as for
 * the settings file's names: ASCII letters only. */
static const char *after_modifier(const char *text, unsigned *flag)
{
    for (size_t i = 0; i < MODIFIER_COUNT; i++) {
        const char *const setting = modifiers[i].setting;
        size_t n = 0;
        while (setting[n] != '\0' && tolower((unsigned char)text[n]) == (unsigned char)setting[n])
            n++;
        if (setting[n] != '\0')
            continue;
        const char *const rest = skip_blanks(text + n);
        if (*rest == MODIFIER_SEPARATOR) {
            *flag = modifiers[i].flag;
            return rest + 1;
        }
    }
    return NULL;
}

bool fastforward_key_from_name(const char *name, struct fastforward_key *out)
{
    unsigned held = 0;
    unsigned flag = 0;
    const char *rest = skip_blanks(name);
    for (const char *next = after_modifier(rest, &flag); next != NULL; next = after_modifier(rest, &flag)) {
        held |= flag;
        rest = skip_blanks(next);
    }
    const int scancode = input_scancode_from_name(rest);
    if (scancode == INPUT_NO_SCANCODE)
        return false;
    *out = (struct fastforward_key){.scancode = scancode, .modifiers = held};
    return true;
}

void fastforward_key_name(const struct fastforward_key *key, char *out, size_t size)
{
    const char *names[MODIFIER_COUNT + 1u];
    size_t count = 0;
    for (size_t i = 0; i < MODIFIER_COUNT; i++) {
        if ((key->modifiers & modifiers[i].flag) != 0)
            names[count++] = modifiers[i].name;
    }
    names[count++] = input_scancode_name(key->scancode);
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < count && used < size; i++) {
        const int written = snprintf(out + used, size - used, i == 0 ? "%s" : "+%s", names[i]);
        if (written < 0)
            return;
        used += (size_t)written;
    }
}

bool fastforward_keys_equal(const struct fastforward_key *a, const struct fastforward_key *b)
{
    return a->scancode == b->scancode && a->modifiers == b->modifiers;
}

/* The state bit a modifier key's own press sets, or 0 for another key. */
static uint16_t own_modifier_bit(int scancode)
{
    for (size_t i = 0; i < MODIFIER_KEY_COUNT; i++) {
        if (modifier_keys[i].scancode == scancode)
            return modifier_keys[i].sdl_bit;
    }
    return 0;
}

bool fastforward_key_pressed(const struct fastforward_key *key, int scancode, uint16_t sdl_modifiers)
{
    if (scancode != key->scancode)
        return false;
    const uint16_t others = (uint16_t)(sdl_modifiers & ~own_modifier_bit(scancode));
    unsigned held = 0;
    for (size_t i = 0; i < MODIFIER_COUNT; i++) {
        if ((others & modifiers[i].sdl_mask) != 0)
            held |= modifiers[i].flag;
    }
    return held == key->modifiers;
}

static bool speed_is_valid(uint32_t speed)
{
    return speed == FASTFORWARD_UNLIMITED || (speed >= FASTFORWARD_MIN_SPEED && speed <= FASTFORWARD_MAX_SPEED);
}

int fastforward_init(const struct fastforward_settings *settings)
{
    if (settings == NULL || fastforward.enabled || !speed_is_valid(settings->speed)
        || fastforward_keys_equal(&settings->hold, &settings->toggle)) {
        (void)fprintf(stderr, MODULE ": fastforward_init needs a speed and two keys, and is called once\n");
        return -1;
    }
    fastforward.settings = *settings;
    fastforward.held = false;
    fastforward.toggled = false;
    fastforward.enabled = true;
    return 0;
}

void fastforward_shutdown(void)
{
    fastforward.enabled = false;
    fastforward.held = false;
    fastforward.toggled = false;
}

bool fastforward_enabled(void)
{
    return fastforward.enabled;
}

/* A press, the overlay's while it is shown; a repeat does nothing. */
static void key_down(const SDL_KeyboardEvent *key)
{
    if (key->repeat != 0 || overlay_shown())
        return;
    const int scancode = (int)key->keysym.scancode;
    if (fastforward_key_pressed(&fastforward.settings.toggle, scancode, key->keysym.mod))
        fastforward.toggled = !fastforward.toggled;
    else if (fastforward_key_pressed(&fastforward.settings.hold, scancode, key->keysym.mod))
        fastforward.held = true;
}

void fastforward_handle_event(const union SDL_Event *event)
{
    if (!fastforward.enabled || event == NULL)
        return;
    switch (event->type) {
    case SDL_KEYDOWN:
        key_down(&event->key);
        break;
    case SDL_KEYUP:
        if ((int)event->key.keysym.scancode == fastforward.settings.hold.scancode)
            fastforward.held = false;
        break;
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            fastforward.held = false;
        break;
    default:
        break;
    }
}

bool fastforward_active(void)
{
    return fastforward.enabled && (fastforward.held || fastforward.toggled);
}

uint32_t fastforward_multiplier(void)
{
    return fastforward_active() ? fastforward.settings.speed : FRAME_PACER_NORMAL_SPEED;
}

bool fastforward_muted(void)
{
    return fastforward_active() && fastforward.settings.audio == FASTFORWARD_AUDIO_MUTE;
}

bool fastforward_indicator_shown(void)
{
    return fastforward_active() && !overlay_shown();
}

void fastforward_indicator_text(uint32_t speed, char *out, size_t size)
{
    if (speed == FASTFORWARD_UNLIMITED)
        (void)snprintf(out, size, UNLIMITED_INDICATOR);
    else
        (void)snprintf(out, size, "FF %lux", (unsigned long)speed);
}

struct fastforward_rect fastforward_indicator_place(const char *text)
{
    /* The last cell's column of space is left out, so that the padding is
     * the same on both sides. */
    const size_t padding = FASTFORWARD_INDICATOR_PADDING;
    const size_t text_width = strlen(text) * FONT_CELL_WIDTH - (FONT_CELL_WIDTH - FONT_GLYPH_WIDTH);
    const size_t width = text_width + 2u * padding;
    return (struct fastforward_rect){
        .x = GBA_LCD_VISIBLE_DOTS - FASTFORWARD_INDICATOR_MARGIN - width,
        .y = FASTFORWARD_INDICATOR_MARGIN,
        .width = width,
        .height = FONT_GLYPH_HEIGHT + 2u * padding,
    };
}

struct fastforward_rect fastforward_indicator_box(void)
{
    char text[FASTFORWARD_INDICATOR_TEXT_SIZE];
    fastforward_indicator_text(fastforward.settings.speed, text, sizeof text);
    return fastforward_indicator_place(text);
}

void fastforward_draw_indicator(const struct font_canvas *canvas)
{
    for (size_t y = 0; y < canvas->height; y++) {
        uint32_t *const row = canvas->pixels + y * canvas->stride;
        for (size_t x = 0; x < canvas->width; x++)
            row[x] = OVERLAY_BACKGROUND_ARGB;
    }
    if (!fastforward.enabled)
        return;
    char text[FASTFORWARD_INDICATOR_TEXT_SIZE];
    fastforward_indicator_text(fastforward.settings.speed, text, sizeof text);
    struct font_pen pen = {
        .x = FASTFORWARD_INDICATOR_PADDING,
        .y = FASTFORWARD_INDICATOR_PADDING,
        .scale = 1u,
        .color = OVERLAY_TEXT_ARGB,
    };
    font_draw_text(canvas, &pen, text, strlen(text));
}
