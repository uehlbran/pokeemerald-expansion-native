/*
 * save_file.h: the cartridge's persistence, the flash image kept in a
 * versioned .sav file.
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
 * That holds for one process per save path: the temporary name is fixed, so
 * two instances storing to the same path can rename each other's half-written
 * file over the save, and nothing locks the path.
 *
 * FILE FORMAT, version 1. A SAVE_FILE_HEADER_SIZE (28) byte header, every
 * integer an unsigned 32-bit little-endian value, then the chip image:
 *     offset  0  magic, the 8 bytes "PENSAVE\0"
 *     offset  8  format version, SAVE_FILE_FORMAT_VERSION (1)
 *     offset 12  layout tag: pointer width in bits of the build that wrote
 *                the image (32 or 64)
 *     offset 16  layout tag: that build's SAVE_FILE_LAYOUT_ID
 *     offset 20  image size, FLASH_CHIP_SIZE (131072)
 *     offset 24  CRC-32/ISO-HDLC of the image (save_file_crc32)
 *     offset 28  the image: byte n is byte n of the chip, sector s at
 *                28 + s * 4096, bank 1 from 28 + 0x10000
 * A bare 131072-byte image (an emulator's .sav, or a native build's from
 * before the header) loads as SAVE_FILE_LEGACY and is taken to be the 32-bit
 * native layout (save_convert.h). An emulator's save is in the GBA's layout,
 * which no native build reads: the game finds its checksums wrong and starts
 * a new game (a 64-bit build first runs the 32-bit converter over it), and
 * the next store overwrites the file. Nothing here probes a bare image for the
 * GBA layout, so copy an emulator's save before trying it.
 * The image in a file this build wrote is its last 131072 bytes
 * (tail -c 131072).
 * The layout tag says which ABI's struct layout the game data in the image
 * has, so a 64-bit build can recognise a 32-bit build's save and refuse or
 * convert it rather than misread it. No checks of the game's sector footers:
 * the game's own GetSaveValidStatus (src/save.c) checks signatures and
 * checksums and falls back to the other save slot, which rejecting a file
 * here would defeat. The ARM7TDMI and x86 are both little-endian, so the
 * game's u16 and u32 footer fields have the same bytes natively as on the
 * GBA (asserted in save_file.c).
 *
 * Use, from main and the frame loop:
 *     flash_init();
 *     save_file_init(path);      // or NULL: in memory only (tests, the test runner)
 *     save_file_load();          // see enum save_file_status; main decides
 *     each frame boundary:  if (flash_take_dirty()) save_file_store();
 *     at exit:              if (flash_take_dirty()) save_file_store();
 *                           save_file_shutdown(); flash_shutdown();
 * Every flash write marks its sector dirty (flash.h), so storing whenever
 * flash_take_dirty() is true persists all of them, the multi-frame saves
 * included: after each frame that writes, as a cartridge holds whatever was
 * written when the power goes. If a store fails, the image is still newer
 * than the file; main reports it and decides whether to retry.
 * A store always writes the running build's layout tag: after
 * SAVE_FILE_FOREIGN_LAYOUT, main must refuse to run the game or convert the
 * image first, or the next store labels a foreign image as native.
 *
 * The sync, the replacement and the directory sync are host_file's (POSIX:
 * fsync, rename over an existing file, fsync of a directory; Windows will
 * need MoveFileEx with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH).
 */
#ifndef SAVE_FILE_H
#define SAVE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host_file.h"

/* Bytes for a path and its terminator, including the ".tmp" store appends:
 * the longest path host_file.h syncs, as the RTC offset file's is. */
#define SAVE_FILE_PATH_SIZE HOST_FILE_PATH_SIZE

/* The format this build writes and the only one it reads with a header. */
#define SAVE_FILE_FORMAT_VERSION 1u
#define SAVE_FILE_HEADER_SIZE 28u

/* Identifies how the platform lays out the game's saved structs
 * (SaveBlock1/2/3, PokemonStorage, the special sectors) inside the image,
 * together with the pointer width: the layout the host compiler gives
 * expansion's structs, which is not the GBA's (the GBA rounds SaveBlock1 to
 * 15568 bytes, i386 to 15496; docs/DECISIONS.md, "Native structs follow the
 * host ABI"), so an emulator's .sav is not interchangeable. The 32- and
 * 64-bit layouts share the id; the pointer width tells them apart, and main
 * moves a save from one to the other with save_convert.h. A build that
 * changes the layout any other way uses a new value. */
#define SAVE_FILE_LAYOUT_ID 1u

/* The ABI an image was written with. */
struct save_file_layout {
    uint32_t pointer_bits; /* sizeof(void *) * 8 of the writing build: 32 or 64 */
    uint32_t layout_id;    /* the writing build's SAVE_FILE_LAYOUT_ID */
};

enum save_file_status {
    /* Loaded a version-1 file written with this build's layout tag. */
    SAVE_FILE_OK = 0,
    /* No file at the path (or a NULL path): the image is an erased chip and
     * the game starts without a save. */
    SAVE_FILE_NEW = 1,
    /* Loaded a bare FLASH_CHIP_SIZE-byte image with no header (an older
     * build's, or an emulator's or a cartridge dump's): the layout is not
     * recorded, and main takes it to be the 32-bit native one; the next store
     * writes it back with a header. */
    SAVE_FILE_LEGACY = 2,
    /* Loaded an intact version-1 file whose layout tag is not this build's
     * (save_file_loaded_layout says which). The image is in the chip, but
     * the game must not read it as native: main refuses or converts it. */
    SAVE_FILE_FOREIGN_LAYOUT = 3,
    /* The file exists but cannot be used: it could not be read; or it has
     * the magic but a truncated header, an unknown version, a pointer width
     * other than 32 or 64, a wrong image size or a CRC that does not match;
     * or it has no magic and is not exactly FLASH_CHIP_SIZE bytes (a
     * 65536-byte 512 Kbit save is one of these; Emerald's chip is 1 Mbit).
     * The reason was printed to stderr; the image is an erased chip; the
     * file is untouched until the game writes and store replaces it. */
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
 * SAVE_FILE_LEGACY, SAVE_FILE_FOREIGN_LAYOUT, SAVE_FILE_CORRUPT or
 * SAVE_FILE_ERR_NOT_INITIALISED. Does not mark the image dirty
 * (flash_take_dirty), so nothing is written back until the game writes. */
int save_file_load(void);

/* The layout tag of the file the last save_file_load read: true and *out
 * filled after SAVE_FILE_OK or SAVE_FILE_FOREIGN_LAYOUT; false (and *out
 * untouched) when that load read no header (NEW, LEGACY, CORRUPT), before
 * any load, and after save_file_shutdown. */
bool save_file_loaded_layout(struct save_file_layout *out);

/* The running build's layout tag, which save_file_store writes:
 * { sizeof(void *) * 8, SAVE_FILE_LAYOUT_ID }. */
struct save_file_layout save_file_native_layout(void);

/* Writes the header (with the running build's layout tag and the image's
 * CRC) and the whole image to <path>.tmp (every fwrite checked), flushes and
 * fsyncs it, closes it, renames it over <path>, and fsyncs the directory.
 * SAVE_FILE_OK, SAVE_FILE_ERR_IO or SAVE_FILE_ERR_NOT_INITIALISED. With a
 * NULL path it writes nothing and returns SAVE_FILE_OK. */
int save_file_store(void);

/* Forgets the path and the loaded layout tag. Safe at any time, including
 * before save_file_init and more than once. Does not store: call
 * save_file_store first if flash_take_dirty() says so. */
void save_file_shutdown(void);

/* CRC-32/ISO-HDLC (the zlib, PNG and Ethernet CRC: reflected polynomial
 * 0xEDB88320, initial value and final XOR 0xFFFFFFFF) of length bytes; 0 for
 * length 0. Its check value, for the ASCII bytes "123456789", is 0xCBF43926. */
uint32_t save_file_crc32(const uint8_t *bytes, size_t length);

#endif /* SAVE_FILE_H */
