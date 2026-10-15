/*
 * data_dir.c: where the player's files live (data_dir.h has the rule).
 *
 * The SDL calls are the two functions behind data_dir_sdl_host; the rest is
 * the rule and the paths, which test/test_data_dir.c drives with a host of
 * its own.
 */
#include "data_dir.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "host_file.h"

#define MODULE "data_dir"

/* Copies the path SDL returned into out and frees SDL's copy. Sizes are
 * printed as unsigned long long: msvcrt's printf has no %zu. */
static bool adopt_sdl_path(char *sdl_path, const char *call, char *out, size_t size)
{
    if (sdl_path == NULL) {
        (void)fprintf(stderr, MODULE ": %s failed: %s\n", call, SDL_GetError());
        return false;
    }
    const size_t length = strlen(sdl_path);
    const bool fits = length < size;
    if (fits)
        memcpy(out, sdl_path, length + 1u);
    else
        (void)fprintf(stderr, MODULE ": %s gave a path of %llu bytes, more than %llu\n", call,
                      (unsigned long long)length, (unsigned long long)(size - 1u));
    SDL_free(sdl_path);
    return fits;
}

static bool sdl_base_dir(char *out, size_t size)
{
    return adopt_sdl_path(SDL_GetBasePath(), "SDL_GetBasePath", out, size);
}

static bool sdl_pref_dir(char *out, size_t size)
{
    return adopt_sdl_path(SDL_GetPrefPath(DATA_DIR_ORGANISATION, DATA_DIR_APPLICATION), "SDL_GetPrefPath", out, size);
}

static const struct data_dir_host sdl_host = {.base_dir = sdl_base_dir, .pref_dir = sdl_pref_dir};

const struct data_dir_host *data_dir_sdl_host(void)
{
    return &sdl_host;
}

bool data_dir_join(const char *dir, const char *name, char *out, size_t size)
{
    const bool ends_in_separator = host_file_name_offset(dir) == strlen(dir);
    const int written = snprintf(out, size, "%s%s%s", dir, ends_in_separator ? "" : "/", name);
    if (written >= 0 && (size_t)written < size)
        return true;
    if (size > 0)
        out[0] = '\0';
    return false;
}

const char *data_dir_mode_name(enum data_dir_mode mode)
{
    switch (mode) {
    case DATA_DIR_GIVEN:
        return "--data-dir";
    case DATA_DIR_PORTABLE:
        return "portable";
    case DATA_DIR_PER_USER:
        return "per-user";
    case DATA_DIR_UNUSED:
    default:
        return "none";
    }
}

static bool copy_path(const char *path, char *out)
{
    const size_t length = strlen(path);
    if (length >= DATA_DIR_PATH_SIZE) {
        (void)fprintf(stderr, MODULE ": the path %s is longer than %llu bytes\n", path,
                      (unsigned long long)(DATA_DIR_PATH_SIZE - 1u));
        return false;
    }
    memcpy(out, path, length + 1u);
    return true;
}

/* The path given on the command line, else name in the data directory. */
static bool place(const char *given, const char *dir, const char *name, char *out)
{
    if (given != NULL)
        return copy_path(given, out);
    if (data_dir_join(dir, name, out, DATA_DIR_PATH_SIZE))
        return true;
    (void)fprintf(stderr, MODULE ": %s in %s would be a path longer than %llu bytes\n", name, dir,
                  (unsigned long long)(DATA_DIR_PATH_SIZE - 1u));
    return false;
}

/* Whether a settings file is at path: one that opens, or one there that
 * may not be read (EACCES), which settings_load then reports rather than
 * have it passed over. */
static bool settings_file_at(const char *path)
{
    const int descriptor = host_file_open(path, O_RDONLY);
    if (descriptor < 0)
        return errno == EACCES;
    if (host_file_close(descriptor) != 0)
        (void)fprintf(stderr, MODULE ": close(%s) failed: %s\n", path, strerror(errno));
    return true;
}

static enum data_dir_error use_given_dir(const char *dir, struct data_paths *out)
{
    if (host_file_make_directory(dir) != 0 && errno != EEXIST) {
        (void)fprintf(stderr, MODULE ": cannot make the data directory %s (--data-dir): %s\n", dir, strerror(errno));
        return DATA_DIR_ERR;
    }
    if (!host_file_is_directory(dir)) { /* EEXIST: something is there, but not a directory */
        (void)fprintf(stderr, MODULE ": %s (--data-dir) is not a directory\n", dir);
        return DATA_DIR_ERR;
    }
    if (!copy_path(dir, out->dir))
        return DATA_DIR_ERR;
    out->mode = DATA_DIR_GIVEN;
    return DATA_DIR_OK;
}

/* Portable mode if the executable's directory holds a settings file. */
static bool use_portable_dir(const struct data_dir_host *host, struct data_paths *out)
{
    char base[DATA_DIR_PATH_SIZE];
    char settings[DATA_DIR_PATH_SIZE];
    if (!host->base_dir(base, sizeof base)) {
        (void)fprintf(stderr, MODULE ": the executable's directory is not known, so portable mode is not checked\n");
        return false;
    }
    if (!data_dir_join(base, DATA_DIR_SETTINGS_NAME, settings, sizeof settings) || !settings_file_at(settings))
        return false;
    memcpy(out->dir, base, sizeof out->dir);
    out->mode = DATA_DIR_PORTABLE;
    return true;
}

static enum data_dir_error find_dir(const struct options *options, const struct data_dir_host *host,
                                    struct data_paths *out)
{
    if (options->data_dir != NULL)
        return use_given_dir(options->data_dir, out);
    if (use_portable_dir(host, out))
        return DATA_DIR_OK;
    if (!host->pref_dir(out->dir, sizeof out->dir)) {
        (void)fprintf(stderr, MODULE ": there is no per-user directory for the save; --data-dir DIR names one\n");
        return DATA_DIR_ERR;
    }
    out->mode = DATA_DIR_PER_USER;
    return DATA_DIR_OK;
}

enum data_dir_error data_dir_resolve(const struct options *options, const struct data_dir_host *host,
                                     struct data_paths *out)
{
    memset(out, 0, sizeof *out);
    if (options->test_runner)
        return DATA_DIR_OK;
    const bool reads_settings = !options->headless || options->keys;
    /* --data-dir is made and checked whenever it is given, so that a wrong
     * one is reported even by a run that gives every file its own path. */
    const bool needs_dir = options->data_dir != NULL || options->save_path == NULL || options->rtc_offset_path == NULL
                           || reads_settings;
    if (needs_dir && find_dir(options, host, out) != DATA_DIR_OK)
        return DATA_DIR_ERR;
    if (!place(options->save_path, out->dir, DATA_DIR_SAVE_NAME, out->save)
        || !place(options->rtc_offset_path, out->dir, DATA_DIR_RTC_OFFSET_NAME, out->rtc_offset))
        return DATA_DIR_ERR;
    if (reads_settings && !place(NULL, out->dir, DATA_DIR_SETTINGS_NAME, out->settings))
        return DATA_DIR_ERR;
    return DATA_DIR_OK;
}
