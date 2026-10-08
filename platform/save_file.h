/*
 * save_file.h: the cartridge's persistence, the flash image kept in a .sav
 * file.
 *
 * Stands in for the non-volatility of the cartridge's flash chip: on the GBA
 * every program and erase is permanent the moment it completes, power or not.
 * Natively the chip is the mapped region flash.h manages, which lives in
 * RAM; this module loads it from a file at start-up and writes it back
 * whenever the game has changed it. Design: docs/RESEARCH.md, "Save". It
 * avoids the ports' mistakes (docs/research/port-*.md): no second file
 * handle for reads (the image in memory is the only copy the game sees),
 * no path that writes flash without persisting, and no write in place: a
 * store goes to <path>.tmp, is flushed and fsynced, and is renamed over
 * <path>, then the directory is fsynced, so a crash or a full disk leaves
 * either the old file or the new one, never a torn one.
 *
 * The file is the raw chip image, FLASH_CHIP_SIZE (131072) bytes, byte n of
 * the file being byte n of the chip: sector s at offset s * 4096, bank 1 from
 * 0x10000. That is the .sav layout GBA emulators use for a 1 Mbit flash
 * cartridge, so a save from an emulator loads here and ours loads there. No
 * header, no checks of the game's sector footers: the game's own
 * GetSaveValidStatus (src/save.c) checks signatures and checksums and falls
 * back to the other save slot, which rejecting a file here would defeat.
 * The ARM7TDMI and x86 are both little-endian, so the game's u16 and u32
 * footer fields have the same bytes natively as on the GBA (asserted in
 * save_file.c).
 *
 * Use, from main and the frame loop:
 *     flash_init();
 *     save_file_init(path);      // or NULL: in memory only (tests, the test runner)
 *     save_file_load();          // SAVE_FILE_OK, _NEW or _CORRUPT; main decides
 *     each frame boundary:  if (flash_take_dirty()) save_file_store();
 *     at exit:              if (flash_take_dirty()) save_file_store();
 *                           save_file_shutdown(); flash_shutdown();
 * save_file_store must be called whenever flash_take_dirty() is true: every
 * path that writes flash goes through the driver functions that set the
 * dirty flag (TrySavingData, the link and record-mixing saves that write a
 * sector at a time, TryWriteSpecialSaveSector, the clear-save and
 * save-failed screens), so this one rule persists all of them. A multi-frame
 * save is stored after each frame it writes, as a real cartridge holds
 * whatever was written when the power goes. If a store fails, the image is
 * still newer than the file; main reports it and decides whether to retry.
 *
 * POSIX only for now (fsync, rename over an existing file, fsync of a
 * directory), like the rest of the platform tests; Windows will need
 * MoveFileEx with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH.
 */
#ifndef SAVE_FILE_H
#define SAVE_FILE_H

/* Bytes for a path and its terminator, including the ".tmp" store appends:
 * Linux's PATH_MAX. */
#define SAVE_FILE_PATH_SIZE 4096u

enum save_file_status {
    SAVE_FILE_OK = 0,
    /* No file at the path (or a NULL path): the image is an erased chip and
     * the game starts without a save. */
    SAVE_FILE_NEW = 1,
    /* The file exists but cannot be used: it could not be read, or it is not
     * a regular file of exactly FLASH_CHIP_SIZE bytes (a 65536-byte 512 Kbit
     * save is one of these; Emerald's chip is 1 Mbit). The reason and the
     * size were printed to stderr; the image is an erased chip; the file is
     * untouched until the game writes and store replaces it. */
    SAVE_FILE_CORRUPT = -1,
    /* A store failed; the call and the error were printed to stderr. The file
     * at the path is the old one (or none), and no <path>.tmp is left, unless
     * the failure was the final directory fsync, after the rename. */
    SAVE_FILE_ERR_IO = -2,
    /* save_file_init got an empty path, one ending in '/', or one too long
     * for SAVE_FILE_PATH_SIZE with ".tmp". */
    SAVE_FILE_ERR_PATH = -3,
    /* save_file_load or save_file_store before save_file_init (or after
     * save_file_shutdown). */
    SAVE_FILE_ERR_NOT_INITIALISED = -4,
};

/* Remembers path (copied) for load and store; NULL means no persistence:
 * load reports SAVE_FILE_NEW with an erased chip and store writes nothing.
 * Touches no file. Returns SAVE_FILE_OK or SAVE_FILE_ERR_PATH (printed);
 * calling it again replaces the path. */
int save_file_init(const char *path);

/* Replaces the flash image with the file's: SAVE_FILE_OK, SAVE_FILE_NEW,
 * SAVE_FILE_CORRUPT or SAVE_FILE_ERR_NOT_INITIALISED. Does not mark the image
 * dirty (flash_take_dirty), so nothing is written back until the game
 * writes. */
int save_file_load(void);

/* Writes the whole image to <path>.tmp (every fwrite checked), flushes and
 * fsyncs it, closes it, renames it over <path>, and fsyncs the directory.
 * SAVE_FILE_OK, SAVE_FILE_ERR_IO or SAVE_FILE_ERR_NOT_INITIALISED. With a
 * NULL path it writes nothing and returns SAVE_FILE_OK. */
int save_file_store(void);

/* Forgets the path. Safe at any time, including before save_file_init and
 * more than once. Does not store: call save_file_store first if
 * flash_take_dirty() says so. */
void save_file_shutdown(void);

#endif /* SAVE_FILE_H */
