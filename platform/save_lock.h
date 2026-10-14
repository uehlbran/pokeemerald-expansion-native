/*
 * save_lock.h: one running game per save file.
 *
 * Stands in for the cartridge being in one console at a time: on the GBA no
 * two running games can write one flash chip. Natively two processes on one
 * save path could, each storing through the same fixed <save>.tmp and renaming
 * the other's half-written file over the save (save_file.h). main takes this
 * lock before any module starts and keeps it until every module has stopped,
 * so that a second instance on the same save refuses to start before it opens
 * a window or reads a file. Test-runner mode writes no save and takes no lock.
 * The owner's ruling (3) of 2026-10-14 and its amendment, docs/DECISIONS.md.
 *
 * The lock is host_file.h's exclusive lock on a companion file beside the
 * save, <save>.lock, created on first use with the mode of every file the
 * platform keeps, and never deleted: its presence means nothing, only the
 * held lock does, and the system drops the lock when the process ends,
 * however it ends, so that a crash or a kill leaves no stale lock behind.
 * Not the save itself: every store replaces the save with a new file
 * (rename), so a lock on the old file would keep out nobody who opens the
 * path after the first store, whereas the companion file is never replaced
 * and its lock outlives every store.
 *
 * A known limit: the lock file is the path as given plus ".lock", so a save
 * reached through another name, a symbolic or a hard link (a/link.sav to
 * a/real.sav), has another lock file, and two instances on the two names are
 * not kept apart. Resolving the path to its file is not worth a host call for
 * a case that takes a link made on purpose.
 *
 * Single-threaded; one lock per process.
 */
#ifndef SAVE_LOCK_H
#define SAVE_LOCK_H

/* Appended to the save's path for the lock file's. */
#define SAVE_LOCK_SUFFIX ".lock"

enum save_lock_status {
    SAVE_LOCK_OK = 0,
    /* Another open of the lock file holds it: another instance of the game
     * is using the save. One line naming the save was printed to stderr. */
    SAVE_LOCK_HELD = -1,
    /* The lock file's path would be too long for host_file.h, or the file
     * could not be opened or locked: printed to stderr, by call. */
    SAVE_LOCK_ERR = -2,
};

/* Takes the lock on <save_path>.lock, creating the file if it is missing,
 * and holds it until save_lock_release or the end of the process. A lock
 * this module held is released first. */
int save_lock_take(const char *save_path);

/* Releases the lock if one is held, by closing its file. Safe at any time
 * and more than once. A failed close is reported; the lock is dropped
 * either way. */
void save_lock_release(void);

#endif /* SAVE_LOCK_H */
