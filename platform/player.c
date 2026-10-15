/*
 * player.c: the player's files, settings and help for this run (player.h).
 */
#include "player.h"

#include <SDL_stdinc.h>

#include "help_text.h"
#include "settings.h"
#include "version.h"

static struct {
    struct data_paths paths;
    struct settings settings;
    struct help_info help;
} player;

/* A path of player.paths, or NULL for a file the run has none of. */
static const char *path_or_null(const char *path)
{
    return path[0] != '\0' ? path : NULL;
}

int player_find(const struct options *options, const struct data_dir_host *host)
{
    if (data_dir_resolve(options, host, &player.paths) != DATA_DIR_OK)
        return -1;
    if (player.paths.settings[0] == '\0')
        settings_defaults(&player.settings);
    else if (settings_load(player.paths.settings, &player.settings) != 0)
        return -1;
    player.help = (struct help_info){
        .version = version_string(),
        .keymap = &player.settings.keymap,
        .pad = &player.settings.pad,
        .fullscreen_key = player.settings.fullscreen_key,
        .fast_forward = &player.settings.fast_forward,
        .paths = &player.paths,
        .settings_found = player.settings.found,
    };
    return 0;
}

const char *player_save_path(void)
{
    return path_or_null(player.paths.save);
}

const char *player_rtc_offset_path(void)
{
    return path_or_null(player.paths.rtc_offset);
}

const struct input_keymap *player_keymap(void)
{
    return &player.settings.keymap;
}

const struct controller_bindings *player_controller(void)
{
    return &player.settings.pad;
}

bool player_starts_fullscreen(const struct options *options)
{
    return player_at_keyboard(options)
           && fullscreen_at_start(player.settings.fullscreen,
                                  fullscreen_on_steam_deck(SDL_getenv(FULLSCREEN_STEAM_DECK_VARIABLE)));
}

int player_fullscreen_key(void)
{
    return player.settings.fullscreen_key;
}

const struct fastforward_settings *player_fast_forward(void)
{
    return &player.settings.fast_forward;
}

int player_window_scale(const struct options *options)
{
    return options->scale_given ? options->scale : player.settings.scale;
}

void player_write_help(overlay_line_sink sink, void *context)
{
    help_text_write(&player.help, sink, context);
}

static void print_line(const char *line, void *context)
{
    FILE *const out = context;
    (void)fprintf(out, "%s\n", line);
}

int player_print_keys(FILE *out)
{
    help_text_write(&player.help, print_line, out);
    if (fflush(out) != 0 || ferror(out)) {
        (void)fprintf(stderr, "player: writing the keys failed\n");
        return -1;
    }
    return 0;
}

bool player_at_keyboard(const struct options *options)
{
    return !options->headless && !options->test_runner && options->replay_path == NULL;
}
