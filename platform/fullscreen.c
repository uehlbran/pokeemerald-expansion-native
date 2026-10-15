/*
 * fullscreen.c: the fullscreen decisions (fullscreen.h).
 */
#include "fullscreen.h"

#include <stdio.h>
#include <string.h>

#include <SDL_events.h>

#define MODULE "fullscreen"

#define STEAM_DECK_VALUE "1"

static struct {
    bool enabled;
    int scancode;
} fullscreen;

bool fullscreen_on_steam_deck(const char *steam_deck)
{
    return steam_deck != NULL && strcmp(steam_deck, STEAM_DECK_VALUE) == 0;
}

bool fullscreen_at_start(enum fullscreen_setting setting, bool on_steam_deck)
{
    if (setting == FULLSCREEN_YES)
        return true;
    if (setting == FULLSCREEN_NO)
        return false;
    return on_steam_deck;
}

int fullscreen_init(int scancode)
{
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES || fullscreen.enabled) {
        (void)fprintf(stderr, MODULE ": fullscreen_init needs a key and is called once\n");
        return -1;
    }
    fullscreen.scancode = scancode;
    fullscreen.enabled = true;
    return 0;
}

void fullscreen_shutdown(void)
{
    fullscreen.enabled = false;
}

bool fullscreen_toggle_requested(const union SDL_Event *event)
{
    return fullscreen.enabled && event != NULL && event->type == SDL_KEYDOWN && event->key.repeat == 0
           && (int)event->key.keysym.scancode == fullscreen.scancode;
}
