/*
 * save_file_fixture.h: what test_save_file.c (loading and the format) and
 * test_save_file_store.c (storing) share. Each is one test binary including
 * this once; the helpers are static inline so a binary need not use them all.
 *
 * The format, written out here from save_file.h's description rather than
 * taken from the module: a 28-byte version-1 header (magic "PENSAVE\0", then
 * version, pointer width, layout id, image size and the image's CRC-32, each
 * a little-endian u32), then the chip image byte for byte (sector s at
 * 28 + s * 4096), 131072 bytes for Emerald's 1 Mbit chip (GBATEK "GBA Cart
 * Backup Flash ROM"). A file without the header, such as the bare 131072-byte
 * image GBA emulators write, is refused.
 *
 * Each test works in a fresh directory under the temporary directory that it
 * creates and removes. It gathers what it checks into locals, cleans up, and
 * only then asserts, so a failing test leaves no files behind.
 */
#ifndef SAVE_FILE_FIXTURE_H
#define SAVE_FILE_FIXTURE_H

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gba/types.h>
#include <gba/flash_internal.h>

#include "flash.h"
#include "ptest.h"
#include "save_file.h"
#include "test_support.h"

#define KIB 1024u
#define SECTOR_BYTES (4 * KIB)
#define CHIP_BYTES (128 * KIB)     /* a 1 Mbit save */
#define HALF_CHIP_BYTES (64 * KIB) /* a 512 Kbit save */
#define ERASED 0xFFu
#define TMP_SUFFIX ".tmp"

/* The version-1 header. */
#define MAGIC "PENSAVE" /* with its terminator, the 8 magic bytes */
#define MAGIC_BYTES 8u
#define VERSION_OFFSET 8u
#define POINTER_BITS_OFFSET 12u
#define LAYOUT_ID_OFFSET 16u
#define IMAGE_SIZE_OFFSET 20u
#define CRC_OFFSET 24u
#define HEADER_BYTES 28u
#define U32_BYTES 4u /* each header integer, least significant byte first */
#define FILE_BYTES (HEADER_BYTES + CHIP_BYTES)

_Static_assert(FLASH_CHIP_SIZE == CHIP_BYTES, "chip size");
_Static_assert(sizeof MAGIC == MAGIC_BYTES, "magic size");
_Static_assert(SAVE_FILE_HEADER_SIZE == HEADER_BYTES, "header size");
_Static_assert(SAVE_FILE_FORMAT_VERSION == 1, "these tests describe format version 1");

/* Whole images and files for expected and read-back contents; static because
 * they are 128 KiB each. */
static uint8_t expected_image[CHIP_BYTES] __attribute__((unused));
static uint8_t file_bytes[FILE_BYTES + 1] __attribute__((unused));
static uint8_t expected_file[FILE_BYTES + 1] __attribute__((unused));

struct scratch {
    char dir[SAVE_FILE_PATH_SIZE];  /* a fresh directory */
    char path[SAVE_FILE_PATH_SIZE]; /* <dir>/emerald.sav */
    char tmp[SAVE_FILE_PATH_SIZE];  /* <path>.tmp */
};

static inline bool format_path(char *out, const char *first, const char *second)
{
    const int length = snprintf(out, SAVE_FILE_PATH_SIZE, "%s%s", first, second);
    return length > 0 && (size_t)length < SAVE_FILE_PATH_SIZE;
}

static inline bool scratch_create(struct scratch *scratch)
{
    if (!tsup_make_temp_directory(scratch->dir, sizeof scratch->dir, "save_file_test"))
        return false;
    if (format_path(scratch->path, scratch->dir, "/emerald.sav")
        && format_path(scratch->tmp, scratch->path, TMP_SUFFIX))
        return true;
    (void)tsup_remove_directory(scratch->dir); /* best-effort: the test fails anyway */
    return false;
}

/* Removes the save, the temporary file and the directory; true if all went. */
static inline bool scratch_remove(const struct scratch *scratch)
{
    const bool save_gone = tsup_remove_file(scratch->path);
    const bool tmp_gone = tsup_remove_file(scratch->tmp);
    return save_gone && tmp_gone && tsup_remove_directory(scratch->dir);
}

static inline bool exists(const char *path)
{
    return tsup_exists(path);
}

/* The file's size, or -1 if it cannot be stat'ed. */
static inline intmax_t file_size(const char *path)
{
    return tsup_file_size(path);
}

static inline bool write_file(const char *path, const uint8_t *bytes, size_t count)
{
    FILE *const file = fopen(path, "wb");
    if (file == NULL)
        return false;
    const bool written = fwrite(bytes, 1, count, file) == count;
    return fclose(file) == 0 && written;
}

/* Reads the whole file into file_bytes; true only if it is exactly count bytes. */
static inline bool read_file(const char *path, size_t count)
{
    FILE *const file = fopen(path, "rb");
    if (file == NULL)
        return false;
    const size_t read = fread(file_bytes, 1, sizeof file_bytes, file);
    return fclose(file) == 0 && read == count;
}

static inline void fill_pattern(uint8_t *bytes, size_t count, uint8_t seed)
{
    for (size_t at = 0; at < count; at++)
        bytes[at] = (uint8_t)(at * 13u + (at >> 12) + seed);
}

static inline size_t first_byte_not(const uint8_t *bytes, size_t count, uint8_t value)
{
    size_t at = 0;
    while (at < count && bytes[at] == value)
        at++;
    return at;
}

static inline bool image_is_erased(void)
{
    return first_byte_not(flash_image(), CHIP_BYTES, ERASED) == CHIP_BYTES;
}

static inline bool image_is_expected(void)
{
    return memcmp(flash_image(), expected_image, CHIP_BYTES) == 0;
}

static inline bool directory_is_empty(const char *path)
{
    return tsup_directory_is_empty(path);
}

/* Puts expected_image, filled from seed, into the chip as the game would
 * leave it, and forgets the dirty flags. */
static inline void set_image(uint8_t seed)
{
    flash_init();
    fill_pattern(expected_image, CHIP_BYTES, seed);
    memcpy(flash_image(), expected_image, CHIP_BYTES);
}

static inline uint32_t get_u32le(const uint8_t *bytes)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < U32_BYTES; i++)
        value |= (uint32_t)bytes[i] << (CHAR_BIT * i);
    return value;
}

static inline void put_u32le(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < U32_BYTES; i++)
        bytes[i] = (uint8_t)(value >> (CHAR_BIT * i));
}

#define POINTER_BITS_32 32u
#define POINTER_BITS_64 64u

static inline uint32_t native_pointer_bits(void)
{
    return (uint32_t)(sizeof(void *) * CHAR_BIT);
}

static inline uint32_t other_pointer_bits(void)
{
    return native_pointer_bits() == POINTER_BITS_32 ? POINTER_BITS_64 : POINTER_BITS_32;
}

/* Puts in file_bytes the version-1 file of expected_image with this layout
 * tag, as the format says a store writes it. */
static inline void build_file(uint32_t pointer_bits, uint32_t layout_id)
{
    memcpy(file_bytes, MAGIC, MAGIC_BYTES);
    put_u32le(file_bytes + VERSION_OFFSET, 1);
    put_u32le(file_bytes + POINTER_BITS_OFFSET, pointer_bits);
    put_u32le(file_bytes + LAYOUT_ID_OFFSET, layout_id);
    put_u32le(file_bytes + IMAGE_SIZE_OFFSET, CHIP_BYTES);
    put_u32le(file_bytes + CRC_OFFSET, save_file_crc32(expected_image, CHIP_BYTES));
    memcpy(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES);
}

#endif /* SAVE_FILE_FIXTURE_H */
