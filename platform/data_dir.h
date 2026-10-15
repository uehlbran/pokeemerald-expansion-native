/*
 * data_dir.h: where the player's files live, the save, the clock's offset
 * and the settings.
 *
 * Not hardware: on the GBA the save and the clock lived on the cartridge,
 * which went wherever the player took it. Natively they are files, and a
 * path relative to wherever the game was started from (a shortcut, another
 * folder) would silently start a fresh game, while the executable's own
 * folder may not be writable (Program Files). So the files live in one data
 * directory (docs/DECISIONS.md, the owner's adjustments to the tasks from the
 * Windows check), found in this order:
 *   1. --data-dir DIR: that directory, made if it is missing (its parent
 *      must exist), refused if something else is there; for the gates and
 *      tests;
 *   2. portable mode: the executable's directory (SDL_GetBasePath), when a
 *      settings file (DATA_DIR_SETTINGS_NAME) is already there;
 *   3. the per-user directory SDL gives (SDL_GetPrefPath, which makes it):
 *      ~/.local/share/<org>/<app>/ on Linux, %APPDATA%\<org>\<app>\ on
 *      Windows, ~/Library/Application Support/<org>/<app>/ on macOS, with
 *      DATA_DIR_ORGANISATION and DATA_DIR_APPLICATION.
 * The save is --save's path if given, else DATA_DIR_SAVE_NAME there; the
 * clock's offset --rtc-offset's, else DATA_DIR_RTC_OFFSET_NAME there; a path
 * given on the command line is used as given (a relative one from the
 * working directory). The settings file is read by a run with a window and
 * by --keys; a headless run has no keyboard and no window for it to set, and
 * reads none. Test-runner mode has no files at all.
 *
 * The directory is looked for only when a file needs it or --data-dir names
 * it, so a headless run given both paths, as every gate's is, never asks SDL
 * for the per-user directory, which SDL would create; and portable mode or
 * --data-dir never asks for it either. --data-dir is made and checked
 * whenever it is given: something there that is not a directory is an
 * error.
 *
 * The rule is data_dir_resolve, pure but for the two questions it puts to
 * the system through data_dir_host (SDL's, data_dir_sdl_host; a test's
 * own) and the file system calls of host_file.h: whether the settings file
 * is beside the executable, and making --data-dir's directory.
 */
#ifndef PLATFORM_DATA_DIR_H
#define PLATFORM_DATA_DIR_H

#include <stdbool.h>
#include <stddef.h>

#include "host_file.h"
#include "options.h"

/* SDL_GetPrefPath's two names. SDL asks for an organisation and an
 * application; the project's own name is the organisation (it claims
 * nothing about pret or rh-hideout), the executable's the application. */
#define DATA_DIR_ORGANISATION "pokeemerald-expansion-native"
#define DATA_DIR_APPLICATION "pokeemerald-native"

/* The files' names in the data directory. */
#define DATA_DIR_SAVE_NAME "pokeemerald.sav"
#define DATA_DIR_RTC_OFFSET_NAME "pokeemerald.rtc"
#define DATA_DIR_SETTINGS_NAME "settings.ini"

/* The room for any of the paths below, the terminating NUL included. */
#define DATA_DIR_PATH_SIZE HOST_FILE_PATH_SIZE

enum data_dir_mode {
    DATA_DIR_UNUSED,   /* no file needed one (test-runner mode; headless with both paths given) */
    DATA_DIR_GIVEN,    /* --data-dir */
    DATA_DIR_PORTABLE, /* the executable's directory, which holds a settings file */
    DATA_DIR_PER_USER  /* SDL's per-user directory */
};

/* Where the files of one run are; "" for one the run has none of. */
struct data_paths {
    enum data_dir_mode mode;
    char dir[DATA_DIR_PATH_SIZE];        /* SDL's end in a separator; --data-dir's is as given; "" when unused */
    char save[DATA_DIR_PATH_SIZE];       /* "" in test-runner mode */
    char rtc_offset[DATA_DIR_PATH_SIZE]; /* "" in test-runner mode */
    char settings[DATA_DIR_PATH_SIZE];   /* "" when the run reads no settings */
};

/* What data_dir_resolve asks the system, each only when it needs the
 * answer. Each writes a directory ending in a separator into out (size
 * bytes) and returns true, or returns false having reported why on stderr. */
struct data_dir_host {
    bool (*base_dir)(char *out, size_t size); /* the executable's directory */
    bool (*pref_dir)(char *out, size_t size); /* the per-user directory, made if missing */
};

enum data_dir_error {
    DATA_DIR_OK = 0,
    DATA_DIR_ERR = -1 /* reported on stderr: no per-user directory, --data-dir not made or not a directory, a
                         path too long */
};

/* SDL's answers: SDL_GetBasePath and SDL_GetPrefPath(DATA_DIR_ORGANISATION,
 * DATA_DIR_APPLICATION). Neither needs SDL_Init. */
const struct data_dir_host *data_dir_sdl_host(void);

/* The files of a run with these options (above). Returns DATA_DIR_OK, or
 * DATA_DIR_ERR after a report on stderr naming the directory or the call;
 * *out is then unspecified. A base directory the host cannot give is
 * reported and portable mode is not checked. */
enum data_dir_error data_dir_resolve(const struct options *options, const struct data_dir_host *host,
                                     struct data_paths *out);

/* Only the directory of the rule above, in out->dir and out->mode, whether or
 * not this run needs a file in it (out is not cleared first): the place of the
 * log of a run with no console (console.h). Returns DATA_DIR_OK, or
 * DATA_DIR_ERR after a report on stderr. */
enum data_dir_error data_dir_find(const struct options *options, const struct data_dir_host *host,
                                  struct data_paths *out);

/* "dir" and "name" as one path: dir as given, a "/" if it does not end in
 * a separator (host_file_name_offset's; "/" works on Windows too), then
 * name. False if the result does not fit size bytes (out is then ""). */
bool data_dir_join(const char *dir, const char *name, char *out, size_t size);

/* How the mode reads in --keys and the overlay: "per-user", "portable",
 * "--data-dir", "none". */
const char *data_dir_mode_name(enum data_dir_mode mode);

#endif /* PLATFORM_DATA_DIR_H */
