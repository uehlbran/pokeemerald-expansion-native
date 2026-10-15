/*
 * test_fullscreen.c: the fullscreen decisions (fullscreen.c): which value of
 * SteamOS's SteamDeck variable is a Deck, whether the window starts
 * fullscreen for each setting on and off a Deck, and the toggle key.
 */
#include "fullscreen.h"
#include "ptest.h"

#include <SDL_events.h>
#include <stdbool.h>

PTEST(a_deck_is_steam_deck_set_to_exactly_1)
{
    PT_ASSERT(fullscreen_on_steam_deck("1"));
}

PTEST(unset_zero_and_other_values_are_not_a_deck)
{
    PT_ASSERT(!fullscreen_on_steam_deck(NULL));
    PT_ASSERT(!fullscreen_on_steam_deck(""));
    PT_ASSERT(!fullscreen_on_steam_deck("0"));
    PT_ASSERT(!fullscreen_on_steam_deck("true"));
    PT_ASSERT(!fullscreen_on_steam_deck("2"));
    PT_ASSERT(!fullscreen_on_steam_deck("11"));
    PT_ASSERT(!fullscreen_on_steam_deck("1 "));
    PT_ASSERT(!fullscreen_on_steam_deck(" 1"));
    PT_ASSERT(!fullscreen_on_steam_deck("yes"));
}

PTEST(auto_starts_fullscreen_on_a_deck_only)
{
    PT_ASSERT(fullscreen_at_start(FULLSCREEN_AUTO, true));
    PT_ASSERT(!fullscreen_at_start(FULLSCREEN_AUTO, false));
}

PTEST(the_setting_overrides_the_machine)
{
    PT_ASSERT(fullscreen_at_start(FULLSCREEN_YES, false));
    PT_ASSERT(fullscreen_at_start(FULLSCREEN_YES, true));
    PT_ASSERT(!fullscreen_at_start(FULLSCREEN_NO, true));
    PT_ASSERT(!fullscreen_at_start(FULLSCREEN_NO, false));
}

static SDL_Event key_event(Uint32 type, SDL_Scancode scancode, Uint8 repeat)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.key.keysym.scancode = scancode;
    event.key.repeat = repeat;
    return event;
}

PTEST(the_key_toggles_on_its_press_only)
{
    fullscreen_shutdown();
    PT_ASSERT_EQ_INT(fullscreen_init(FULLSCREEN_DEFAULT_SCANCODE), 0);
    const SDL_Event down = key_event(SDL_KEYDOWN, SDL_SCANCODE_F11, 0);
    const SDL_Event repeat = key_event(SDL_KEYDOWN, SDL_SCANCODE_F11, 1);
    const SDL_Event up = key_event(SDL_KEYUP, SDL_SCANCODE_F11, 0);
    const SDL_Event other = key_event(SDL_KEYDOWN, SDL_SCANCODE_F10, 0);
    const bool pressed = fullscreen_toggle_requested(&down);
    const bool repeated = fullscreen_toggle_requested(&repeat);
    const bool released = fullscreen_toggle_requested(&up);
    const bool elsewhere = fullscreen_toggle_requested(&other);
    const bool none = fullscreen_toggle_requested(NULL);
    fullscreen_shutdown();
    PT_ASSERT(pressed);
    PT_ASSERT(!repeated);
    PT_ASSERT(!released);
    PT_ASSERT(!elsewhere);
    PT_ASSERT(!none);
}

PTEST(a_rebound_key_replaces_f11)
{
    fullscreen_shutdown();
    PT_ASSERT_EQ_INT(fullscreen_init(SDL_SCANCODE_F12), 0);
    const SDL_Event f11 = key_event(SDL_KEYDOWN, SDL_SCANCODE_F11, 0);
    const SDL_Event f12 = key_event(SDL_KEYDOWN, SDL_SCANCODE_F12, 0);
    const bool old_key = fullscreen_toggle_requested(&f11);
    const bool new_key = fullscreen_toggle_requested(&f12);
    fullscreen_shutdown();
    PT_ASSERT(!old_key);
    PT_ASSERT(new_key);
}

PTEST(the_key_does_nothing_before_init_and_after_shutdown)
{
    const SDL_Event down = key_event(SDL_KEYDOWN, SDL_SCANCODE_F11, 0);
    fullscreen_shutdown();
    PT_ASSERT(!fullscreen_toggle_requested(&down));
    PT_ASSERT_EQ_INT(fullscreen_init(FULLSCREEN_DEFAULT_SCANCODE), 0);
    fullscreen_shutdown();
    PT_ASSERT(!fullscreen_toggle_requested(&down));
}

PTEST(init_refuses_no_key_an_out_of_range_key_and_a_second_init)
{
    fullscreen_shutdown();
    PT_ASSERT_EQ_INT(fullscreen_init(SDL_SCANCODE_UNKNOWN), -1);
    PT_ASSERT_EQ_INT(fullscreen_init(-3), -1);
    PT_ASSERT_EQ_INT(fullscreen_init(SDL_NUM_SCANCODES), -1);
    PT_ASSERT_EQ_INT(fullscreen_init(SDL_SCANCODE_F11), 0);
    PT_ASSERT_EQ_INT(fullscreen_init(SDL_SCANCODE_F12), -1);
    fullscreen_shutdown();
}

PTEST_MAIN()
