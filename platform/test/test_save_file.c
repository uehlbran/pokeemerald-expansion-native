/*
 * Tests of the save file, save_file.c. The format, written out here from
 * save_file.h's description rather than taken from the module: a 28-byte
 * version-1 header (magic "PENSAVE\0", then version, pointer width, layout
 * id, image size and the image's CRC-32, each a little-endian u32), then the
 * chip image byte for byte (sector s at 28 + s * 4096), 131072 bytes for
 * Emerald's 1 Mbit chip (GBATEK "GBA Cart Backup Flash ROM"). A bare
 * 131072-byte image, what GBA emulators write, is a legacy file.
 *
 * Each test works in a fresh directory under $TMPDIR (or /tmp) that it
 * creates and removes. It gathers what it checks into locals, cleans up, and
 * only then asserts, so a failing test leaves no files behind. Failures are
 * provoked in ways that work for any user, root included: a file size limit
 * (RLIMIT_FSIZE) for a write that fails part-way, a directory where the
 * temporary file or the save must go for an open or rename that fails.
 */
// deps: gba_memmap.c flash.c
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gba/types.h>
#include <gba/flash_internal.h>

#include "flash.h"
#include "ptest.h"
#include "save_file.h"

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
#define FILE_BYTES (HEADER_BYTES + CHIP_BYTES)

_Static_assert(FLASH_CHIP_SIZE == CHIP_BYTES, "chip size");
_Static_assert(sizeof MAGIC == MAGIC_BYTES, "magic size");
_Static_assert(SAVE_FILE_HEADER_SIZE == HEADER_BYTES, "header size");
_Static_assert(SAVE_FILE_FORMAT_VERSION == 1, "these tests describe format version 1");

/* Whole images and files for expected and read-back contents; static because
 * they are 128 KiB each. */
static uint8_t expected_image[CHIP_BYTES];
static uint8_t file_bytes[FILE_BYTES + 1];

struct scratch {
    char dir[SAVE_FILE_PATH_SIZE];  /* a fresh directory */
    char path[SAVE_FILE_PATH_SIZE]; /* <dir>/emerald.sav */
    char tmp[SAVE_FILE_PATH_SIZE];  /* <path>.tmp */
};

static bool format_path(char *out, const char *first, const char *second)
{
    const int length = snprintf(out, SAVE_FILE_PATH_SIZE, "%s%s", first, second);
    return length > 0 && (size_t)length < SAVE_FILE_PATH_SIZE;
}

static bool scratch_create(struct scratch *scratch)
{
    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0')
        base = "/tmp";
    if (!format_path(scratch->dir, base, "/save_file_test.XXXXXX"))
        return false;
    if (mkdtemp(scratch->dir) == NULL)
        return false;
    if (format_path(scratch->path, scratch->dir, "/emerald.sav")
        && format_path(scratch->tmp, scratch->path, TMP_SUFFIX))
        return true;
    (void)rmdir(scratch->dir); /* best-effort: the test fails anyway */
    return false;
}

static bool remove_if_present(const char *path)
{
    return remove(path) == 0 || errno == ENOENT;
}

/* Removes the save, the temporary file and the directory; true if all went. */
static bool scratch_remove(const struct scratch *scratch)
{
    const bool save_gone = remove_if_present(scratch->path);
    const bool tmp_gone = remove_if_present(scratch->tmp);
    return save_gone && tmp_gone && rmdir(scratch->dir) == 0;
}

static bool exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

/* The file's size, or -1 if it cannot be stat'ed. */
static intmax_t file_size(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 ? (intmax_t)info.st_size : -1;
}

static bool write_file(const char *path, const uint8_t *bytes, size_t count)
{
    FILE *const file = fopen(path, "wb");
    if (file == NULL)
        return false;
    const bool written = fwrite(bytes, 1, count, file) == count;
    return fclose(file) == 0 && written;
}

/* Reads the whole file into file_bytes; true only if it is exactly count bytes. */
static bool read_file(const char *path, size_t count)
{
    FILE *const file = fopen(path, "rb");
    if (file == NULL)
        return false;
    const size_t read = fread(file_bytes, 1, sizeof file_bytes, file);
    return fclose(file) == 0 && read == count;
}

static void fill_pattern(uint8_t *bytes, size_t count, uint8_t seed)
{
    for (size_t at = 0; at < count; at++)
        bytes[at] = (uint8_t)(at * 13u + (at >> 12) + seed);
}

static size_t first_byte_not(const uint8_t *bytes, size_t count, uint8_t value)
{
    size_t at = 0;
    while (at < count && bytes[at] == value)
        at++;
    return at;
}

static bool image_is_erased(void)
{
    return first_byte_not(flash_image(), CHIP_BYTES, ERASED) == CHIP_BYTES;
}

static bool image_is_expected(void)
{
    return memcmp(flash_image(), expected_image, CHIP_BYTES) == 0;
}

static bool directory_is_empty(const char *path)
{
    DIR *const dir = opendir(path);
    if (dir == NULL)
        return false;
    size_t entries = 0;
    for (const struct dirent *entry = readdir(dir); entry != NULL; entry = readdir(dir))
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            entries++;
    return closedir(dir) == 0 && entries == 0;
}

/* Puts expected_image, filled from seed, into the chip as the game would
 * leave it, and forgets the dirty flags. */
static void set_image(uint8_t seed)
{
    flash_init();
    fill_pattern(expected_image, CHIP_BYTES, seed);
    memcpy(flash_image(), expected_image, CHIP_BYTES);
}

static uint32_t get_u32le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16
           | (uint32_t)bytes[3] << 24;
}

static void put_u32le(uint8_t *bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; i++)
        bytes[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t native_pointer_bits(void)
{
    return (uint32_t)(sizeof(void *) * 8);
}

static uint32_t other_pointer_bits(void)
{
    return native_pointer_bits() == 32 ? 64 : 32;
}

/* Puts in file_bytes the version-1 file of expected_image with this layout
 * tag, as the format says a store writes it. */
static void build_file(uint32_t pointer_bits, uint32_t layout_id)
{
    memcpy(file_bytes, MAGIC, MAGIC_BYTES);
    put_u32le(file_bytes + VERSION_OFFSET, 1);
    put_u32le(file_bytes + POINTER_BITS_OFFSET, pointer_bits);
    put_u32le(file_bytes + LAYOUT_ID_OFFSET, layout_id);
    put_u32le(file_bytes + IMAGE_SIZE_OFFSET, CHIP_BYTES);
    put_u32le(file_bytes + CRC_OFFSET, save_file_crc32(expected_image, CHIP_BYTES));
    memcpy(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES);
}

/* ---- the CRC and the layout tag ---- */

PTEST(crc32_matches_the_standard_check_values)
{
    static const uint8_t check[] = "123456789";
    static const uint8_t fox[] = "The quick brown fox jumps over the lazy dog";
    static const uint8_t a[] = "a";
    PT_ASSERT_EQ_U32(save_file_crc32(check, sizeof check - 1), UINT32_C(0xCBF43926));
    PT_ASSERT_EQ_U32(save_file_crc32(fox, sizeof fox - 1), UINT32_C(0x414FA339));
    PT_ASSERT_EQ_U32(save_file_crc32(a, 1), UINT32_C(0xE8B7BE43));
    PT_ASSERT_EQ_U32(save_file_crc32(check, 0), 0);
}

PTEST(native_layout_is_this_builds_pointer_width_and_layout_id)
{
    const struct save_file_layout native = save_file_native_layout();
    PT_ASSERT_EQ_U32(native.pointer_bits, native_pointer_bits());
    PT_ASSERT_EQ_U32(native.layout_id, SAVE_FILE_LAYOUT_ID);
}

PTEST(no_layout_is_reported_before_a_load)
{
    save_file_shutdown();
    struct save_file_layout layout = { 7, 7 };
    PT_ASSERT(!save_file_loaded_layout(&layout));
    PT_ASSERT_EQ_U32(layout.pointer_bits, 7); /* untouched */
}

/* ---- before init ---- */

PTEST(load_and_store_without_init_fail_and_touch_nothing)
{
    save_file_shutdown();
    save_file_shutdown();
    set_image(0x01);
    const int load = save_file_load();
    const int store = save_file_store();
    PT_ASSERT_EQ_INT(load, SAVE_FILE_ERR_NOT_INITIALISED);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_ERR_NOT_INITIALISED);
    PT_ASSERT(image_is_expected());
}

PTEST(load_and_store_after_shutdown_fail)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x02);
    const int init = save_file_init(scratch.path);
    save_file_shutdown();
    const int load = save_file_load();
    const int store = save_file_store();
    const bool created = exists(scratch.path) || exists(scratch.tmp);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_ERR_NOT_INITIALISED);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_ERR_NOT_INITIALISED);
    PT_ASSERT(!created);
    PT_ASSERT(image_is_expected());
    PT_ASSERT(removed);
}

PTEST(init_rejects_unusable_paths)
{
    /* The longest path that fits with ".tmp" and the terminator, and one more. */
    static char longest[SAVE_FILE_PATH_SIZE];
    const size_t longest_length = SAVE_FILE_PATH_SIZE - sizeof TMP_SUFFIX;
    memset(longest, 'a', longest_length);
    longest[longest_length] = '\0';
    static char too_long[SAVE_FILE_PATH_SIZE];
    memset(too_long, 'a', longest_length + 1);
    too_long[longest_length + 1] = '\0';

    const int empty = save_file_init("");
    const int load_after_empty = save_file_load();
    const int directory = save_file_init("saves/");
    const int too_long_status = save_file_init(too_long);
    const int load_after_too_long = save_file_load();
    const int fits = save_file_init(longest);
    save_file_shutdown();
    PT_ASSERT_EQ_INT(empty, SAVE_FILE_ERR_PATH);
    PT_ASSERT_EQ_INT(load_after_empty, SAVE_FILE_ERR_NOT_INITIALISED);
    PT_ASSERT_EQ_INT(directory, SAVE_FILE_ERR_PATH);
    PT_ASSERT_EQ_INT(too_long_status, SAVE_FILE_ERR_PATH);
    PT_ASSERT_EQ_INT(load_after_too_long, SAVE_FILE_ERR_NOT_INITIALISED);
    PT_ASSERT_EQ_INT(fits, SAVE_FILE_OK);
}

/* ---- load ---- */

PTEST(missing_file_loads_as_a_new_erased_chip)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x03);
    const int init = save_file_init(scratch.path);
    const int load = save_file_load();
    const bool erased = image_is_erased();
    const bool dirty = flash_take_dirty();
    const bool created = exists(scratch.path) || exists(scratch.tmp);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_NEW);
    PT_ASSERT(erased);
    PT_ASSERT(!dirty);
    PT_ASSERT(!created);
    PT_ASSERT(removed);
}

/* A .sav as an emulator writes it: the chip's bytes in order, no header. It
 * loads as a legacy file of unknown layout, sector s at s * 4096 with banks
 * included, and loading is not a game write. */
PTEST(an_emulator_save_loads_as_legacy_byte_for_byte)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    flash_init();
    fill_pattern(expected_image, CHIP_BYTES, 0x04);
    const bool written = write_file(scratch.path, expected_image, CHIP_BYTES);
    const int init = save_file_init(scratch.path);
    const int load = save_file_load();
    const bool loaded = image_is_expected();
    const bool dirty = flash_take_dirty();
    struct save_file_layout layout;
    const bool has_layout = save_file_loaded_layout(&layout);
    uint8_t sector_17[SECTOR_BYTES];
    ReadFlash(17, 0, sector_17, SECTOR_BYTES);
    const bool sector_17_matches =
        memcmp(sector_17, expected_image + 17 * SECTOR_BYTES, SECTOR_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_LEGACY);
    PT_ASSERT(loaded);
    PT_ASSERT(sector_17_matches);
    PT_ASSERT(!dirty);
    PT_ASSERT(!has_layout);
    PT_ASSERT(removed);
}

/* The first size bytes of file_bytes, as a save file, are reported as
 * corrupt: the chip is left erased (not partly loaded, not left as it was),
 * nothing is marked dirty, no layout is reported, and the file stays. */
static void check_rejected_file(size_t size)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool written = write_file(scratch.path, file_bytes, size);
    set_image(0x06);
    const int init = save_file_init(scratch.path);
    const int load = save_file_load();
    const bool erased = image_is_erased();
    const bool dirty = flash_take_dirty();
    struct save_file_layout layout;
    const bool has_layout = save_file_loaded_layout(&layout);
    const intmax_t size_after = file_size(scratch.path);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_CORRUPT);
    PT_ASSERT(erased);
    PT_ASSERT(!dirty);
    PT_ASSERT(!has_layout);
    PT_ASSERT_EQ_INT(size_after, (intmax_t)size);
    PT_ASSERT(removed);
}

/* A file with no header and the wrong size for a bare image. */
static void check_rejected_size(size_t size)
{
    fill_pattern(file_bytes, size, 0x05);
    check_rejected_file(size);
}

PTEST(a_512_kbit_save_is_rejected) { check_rejected_size(HALF_CHIP_BYTES); }
PTEST(a_file_one_byte_too_long_is_rejected) { check_rejected_size(CHIP_BYTES + 1); }
PTEST(a_file_one_byte_too_short_is_rejected) { check_rejected_size(CHIP_BYTES - 1); }
PTEST(an_empty_file_is_rejected) { check_rejected_size(0); }

/* ---- the header ---- */

PTEST(a_crc_mismatch_in_the_image_is_rejected)
{
    fill_pattern(expected_image, CHIP_BYTES, 0x21);
    build_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    file_bytes[HEADER_BYTES + 0x11234] ^= 0x04;
    check_rejected_file(FILE_BYTES);
}

PTEST(a_wrong_crc_field_is_rejected)
{
    fill_pattern(expected_image, CHIP_BYTES, 0x22);
    build_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    file_bytes[CRC_OFFSET + 3] ^= 0x80;
    check_rejected_file(FILE_BYTES);
}

static void check_rejected_header_field(size_t offset, uint32_t value)
{
    fill_pattern(expected_image, CHIP_BYTES, 0x23);
    build_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    put_u32le(file_bytes + offset, value);
    check_rejected_file(FILE_BYTES);
}

PTEST(version_2_is_rejected) { check_rejected_header_field(VERSION_OFFSET, 2); }
PTEST(version_0_is_rejected) { check_rejected_header_field(VERSION_OFFSET, 0); }
PTEST(a_wrong_image_size_field_is_rejected)
{
    check_rejected_header_field(IMAGE_SIZE_OFFSET, HALF_CHIP_BYTES);
}
PTEST(a_pointer_width_other_than_32_or_64_is_rejected)
{
    check_rejected_header_field(POINTER_BITS_OFFSET, 48);
}

PTEST(a_truncated_header_is_rejected)
{
    fill_pattern(expected_image, CHIP_BYTES, 0x24);
    build_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    check_rejected_file(HEADER_BYTES - 1);
}

PTEST(the_magic_alone_is_rejected)
{
    fill_pattern(expected_image, CHIP_BYTES, 0x25);
    build_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    check_rejected_file(MAGIC_BYTES);
}

PTEST(a_truncated_image_is_rejected)
{
    fill_pattern(expected_image, CHIP_BYTES, 0x26);
    build_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    check_rejected_file(FILE_BYTES - 1);
}

PTEST(a_headered_file_one_byte_too_long_is_rejected)
{
    fill_pattern(expected_image, CHIP_BYTES, 0x27);
    build_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    file_bytes[FILE_BYTES] = 0x00;
    check_rejected_file(FILE_BYTES + 1);
}

/* A bare image of exactly the chip's size that happens to start with the
 * magic is a headered file of the wrong size, not a legacy file. */
PTEST(a_bare_image_starting_with_the_magic_is_rejected)
{
    fill_pattern(file_bytes, CHIP_BYTES, 0x28);
    memcpy(file_bytes, MAGIC, MAGIC_BYTES);
    check_rejected_file(CHIP_BYTES);
}

struct layout_outcome {
    int load;
    bool loaded, dirty, has_layout;
    struct save_file_layout layout;
};

/* Writes the version-1 file of a fresh image with this layout tag and loads
 * it. */
static bool load_tagged_file(uint32_t pointer_bits, uint32_t layout_id,
                             struct layout_outcome *outcome)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        return false;
    fill_pattern(expected_image, CHIP_BYTES, 0x29);
    build_file(pointer_bits, layout_id);
    const bool written = write_file(scratch.path, file_bytes, FILE_BYTES);
    flash_init();
    const bool init = save_file_init(scratch.path) == SAVE_FILE_OK;
    outcome->load = save_file_load();
    outcome->loaded = image_is_expected();
    outcome->dirty = flash_take_dirty();
    outcome->has_layout = save_file_loaded_layout(&outcome->layout);
    save_file_shutdown();
    return scratch_remove(&scratch) && written && init;
}

PTEST(a_file_with_the_native_tag_loads_ok_and_reports_its_layout)
{
    struct layout_outcome outcome = { 0 };
    PT_ASSERT(load_tagged_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID, &outcome));
    PT_ASSERT_EQ_INT(outcome.load, SAVE_FILE_OK);
    PT_ASSERT(outcome.loaded);
    PT_ASSERT(!outcome.dirty);
    PT_ASSERT(outcome.has_layout);
    PT_ASSERT_EQ_U32(outcome.layout.pointer_bits, native_pointer_bits());
    PT_ASSERT_EQ_U32(outcome.layout.layout_id, SAVE_FILE_LAYOUT_ID);
}

/* A save written by the other bit width loads, intact, but is reported as
 * foreign with its tag, never as SAVE_FILE_OK. */
PTEST(a_file_from_the_other_pointer_width_loads_as_foreign)
{
    struct layout_outcome outcome = { 0 };
    PT_ASSERT(load_tagged_file(other_pointer_bits(), SAVE_FILE_LAYOUT_ID, &outcome));
    PT_ASSERT_EQ_INT(outcome.load, SAVE_FILE_FOREIGN_LAYOUT);
    PT_ASSERT(outcome.loaded);
    PT_ASSERT(!outcome.dirty);
    PT_ASSERT(outcome.has_layout);
    PT_ASSERT_EQ_U32(outcome.layout.pointer_bits, other_pointer_bits());
    PT_ASSERT_EQ_U32(outcome.layout.layout_id, SAVE_FILE_LAYOUT_ID);
}

PTEST(a_file_with_another_layout_id_loads_as_foreign)
{
    struct layout_outcome outcome = { 0 };
    PT_ASSERT(load_tagged_file(native_pointer_bits(), SAVE_FILE_LAYOUT_ID + 1, &outcome));
    PT_ASSERT_EQ_INT(outcome.load, SAVE_FILE_FOREIGN_LAYOUT);
    PT_ASSERT(outcome.loaded);
    PT_ASSERT(outcome.has_layout);
    PT_ASSERT_EQ_U32(outcome.layout.pointer_bits, native_pointer_bits());
    PT_ASSERT_EQ_U32(outcome.layout.layout_id, SAVE_FILE_LAYOUT_ID + 1);
}

/* A legacy file loads, and the next store writes it back with a header and
 * this build's tag; loading that gives SAVE_FILE_OK. */
PTEST(a_legacy_file_is_upgraded_by_the_next_store)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    fill_pattern(expected_image, CHIP_BYTES, 0x2A);
    const bool written = write_file(scratch.path, expected_image, CHIP_BYTES);
    flash_init();
    const int init = save_file_init(scratch.path);
    const int first_load = save_file_load();
    const int store = save_file_store();
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool has_magic = memcmp(file_bytes, MAGIC, MAGIC_BYTES) == 0;
    const bool image_after_header =
        memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    memset(flash_image(), 0x00, CHIP_BYTES);
    const int second_load = save_file_load();
    const bool loaded = image_is_expected();
    struct save_file_layout layout = { 0 };
    const bool has_layout = save_file_loaded_layout(&layout);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first_load, SAVE_FILE_LEGACY);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT(read);
    PT_ASSERT(has_magic);
    PT_ASSERT(image_after_header);
    PT_ASSERT_EQ_INT(second_load, SAVE_FILE_OK);
    PT_ASSERT(loaded);
    PT_ASSERT(has_layout);
    PT_ASSERT_EQ_U32(layout.pointer_bits, native_pointer_bits());
    PT_ASSERT(removed);
}

/* After a headered load, a load that reads no header and a shutdown both
 * forget the layout. */
PTEST(the_loaded_layout_is_forgotten_by_a_headerless_load_and_shutdown)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x2B);
    struct save_file_layout layout;
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    const int headered = save_file_load();
    const bool after_headered = save_file_loaded_layout(&layout);
    const bool rewritten = write_file(scratch.path, expected_image, CHIP_BYTES);
    const int legacy = save_file_load();
    const bool after_legacy = save_file_loaded_layout(&layout);
    const int headered_again = save_file_store() == SAVE_FILE_OK ? save_file_load() : -99;
    save_file_shutdown();
    const bool after_shutdown = save_file_loaded_layout(&layout);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(headered, SAVE_FILE_OK);
    PT_ASSERT(after_headered);
    PT_ASSERT(rewritten);
    PT_ASSERT_EQ_INT(legacy, SAVE_FILE_LEGACY);
    PT_ASSERT(!after_legacy);
    PT_ASSERT_EQ_INT(headered_again, SAVE_FILE_OK);
    PT_ASSERT(!after_shutdown);
    PT_ASSERT(removed);
}

PTEST(a_directory_at_the_save_path_is_rejected)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool made = mkdir(scratch.path, 0700) == 0;
    set_image(0x07);
    const int init = save_file_init(scratch.path);
    const int load = save_file_load();
    const bool erased = image_is_erased();
    save_file_shutdown();
    const bool directory_removed = rmdir(scratch.path) == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(made);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_CORRUPT);
    PT_ASSERT(erased);
    PT_ASSERT(directory_removed);
    PT_ASSERT(removed);
}

/* ---- store ---- */

PTEST(store_then_load_round_trips_byte_exactly)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x08);
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    const intmax_t size = file_size(scratch.path);
    const bool tmp_left = exists(scratch.tmp);
    memset(flash_image(), 0x00, CHIP_BYTES);
    const int load = save_file_load();
    const bool round_trip = image_is_expected();
    struct save_file_layout layout = { 0 };
    const bool has_layout = save_file_loaded_layout(&layout);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(size, FILE_BYTES);
    PT_ASSERT(!tmp_left);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_OK);
    PT_ASSERT(round_trip);
    PT_ASSERT(has_layout);
    PT_ASSERT_EQ_U32(layout.pointer_bits, native_pointer_bits());
    PT_ASSERT_EQ_U32(layout.layout_id, SAVE_FILE_LAYOUT_ID);
    PT_ASSERT(removed);
}

/* The stored file is the version-1 header, with this build's tag and the
 * image's CRC, then the chip's bytes in order; a game write through the
 * driver is in it at its sector's offset. */
PTEST(stored_file_is_the_header_then_the_chip_byte_for_byte)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x09);
    uint8_t sector[SECTOR_BYTES];
    fill_pattern(sector, sizeof sector, 0x99);
    const uint16_t program = NativeFlash.programFlashSector(31, sector);
    memcpy(expected_image + 31 * SECTOR_BYTES, sector, SECTOR_BYTES);
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool same = memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_U32(program, FLASH_STATUS_OK);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT(read);
    PT_ASSERT_MEM_EQ(file_bytes, MAGIC, MAGIC_BYTES);
    PT_ASSERT_EQ_U32(get_u32le(file_bytes + VERSION_OFFSET), 1);
    PT_ASSERT_EQ_U32(get_u32le(file_bytes + POINTER_BITS_OFFSET), native_pointer_bits());
    PT_ASSERT_EQ_U32(get_u32le(file_bytes + LAYOUT_ID_OFFSET), SAVE_FILE_LAYOUT_ID);
    PT_ASSERT_EQ_U32(get_u32le(file_bytes + IMAGE_SIZE_OFFSET), CHIP_BYTES);
    PT_ASSERT_EQ_U32(get_u32le(file_bytes + CRC_OFFSET),
                     save_file_crc32(expected_image, CHIP_BYTES));
    PT_ASSERT(same);
    PT_ASSERT(removed);
}

PTEST(store_replaces_the_previous_file)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x0A);
    const int init = save_file_init(scratch.path);
    const int first = save_file_store();
    set_image(0x0B);
    const int second = save_file_store();
    const intmax_t size = file_size(scratch.path);
    const bool tmp_left = exists(scratch.tmp);
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool newest = memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(second, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(size, FILE_BYTES);
    PT_ASSERT(!tmp_left);
    PT_ASSERT(read);
    PT_ASSERT(newest);
    PT_ASSERT(removed);
}

/* The file size limit makes the write to <path>.tmp fail after 64 KiB, as a
 * full disk would. The old save must be intact and the partial tmp gone. */
PTEST(a_write_that_fails_part_way_leaves_the_old_file)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x0C);
    const int init = save_file_init(scratch.path);
    const int first = save_file_store();
    set_image(0x0D);

    struct rlimit old_limit = { 0 };
    const bool got_limit = getrlimit(RLIMIT_FSIZE, &old_limit) == 0;
    const struct rlimit low_limit = { HALF_CHIP_BYTES, old_limit.rlim_max };
    void (*const old_handler)(int) = signal(SIGXFSZ, SIG_IGN);
    const bool limited = got_limit && old_handler != SIG_ERR
                         && setrlimit(RLIMIT_FSIZE, &low_limit) == 0;
    const int second = limited ? save_file_store() : SAVE_FILE_OK;
    const bool restored = got_limit && setrlimit(RLIMIT_FSIZE, &old_limit) == 0
                          && old_handler != SIG_ERR && signal(SIGXFSZ, old_handler) != SIG_ERR;

    fill_pattern(expected_image, CHIP_BYTES, 0x0C); /* the old save */
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool old_intact = memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    const bool tmp_left = exists(scratch.tmp);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(limited);
    PT_ASSERT(restored);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(second, SAVE_FILE_ERR_IO);
    PT_ASSERT(read);
    PT_ASSERT(old_intact);
    PT_ASSERT(!tmp_left);
    PT_ASSERT(removed);
}

/* <path>.tmp cannot be created (a directory is in the way): the store fails
 * before touching anything, and leaves alone what it did not create. */
PTEST(a_tmp_that_cannot_be_created_leaves_the_old_file)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x0E);
    const int init = save_file_init(scratch.path);
    const int first = save_file_store();
    const bool made = mkdir(scratch.tmp, 0700) == 0;
    set_image(0x0F);
    const int second = save_file_store();
    fill_pattern(expected_image, CHIP_BYTES, 0x0E); /* the old save */
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool old_intact = memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    const bool directory_kept = rmdir(scratch.tmp) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first, SAVE_FILE_OK);
    PT_ASSERT(made);
    PT_ASSERT_EQ_INT(second, SAVE_FILE_ERR_IO);
    PT_ASSERT(read);
    PT_ASSERT(old_intact);
    PT_ASSERT(directory_kept);
    PT_ASSERT(removed);
}

/* The rename fails (a directory is at the save path): no tmp is left. */
PTEST(a_failed_rename_leaves_no_tmp)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool made = mkdir(scratch.path, 0700) == 0;
    set_image(0x10);
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    const bool tmp_left = exists(scratch.tmp);
    const bool directory_kept = rmdir(scratch.path) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(made);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_ERR_IO);
    PT_ASSERT(!tmp_left);
    PT_ASSERT(directory_kept);
    PT_ASSERT(removed);
}

/* ---- paths relative to the working directory, and no path at all ---- */

/* Runs body with the working directory set to dir; false if it cannot. */
static bool in_directory(const char *dir, void (*body)(void *), void *context)
{
    const int previous = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (previous < 0)
        return false;
    bool ok = chdir(dir) == 0;
    if (ok)
        body(context);
    ok = fchdir(previous) == 0 && ok;
    return close(previous) == 0 && ok;
}

struct null_path_outcome {
    int init, store, load;
    bool stored_nothing, erased;
};

static void use_null_path(void *context)
{
    struct null_path_outcome *const outcome = context;
    set_image(0x11);
    outcome->init = save_file_init(NULL);
    outcome->store = save_file_store();
    outcome->stored_nothing = directory_is_empty(".");
    outcome->load = save_file_load();
    outcome->erased = image_is_erased();
    save_file_shutdown();
}

PTEST(null_path_stores_nothing_and_loads_new)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    struct null_path_outcome outcome = { 0 };
    const bool ran = in_directory(scratch.dir, use_null_path, &outcome);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(ran);
    PT_ASSERT_EQ_INT(outcome.init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(outcome.store, SAVE_FILE_OK);
    PT_ASSERT(outcome.stored_nothing);
    PT_ASSERT_EQ_INT(outcome.load, SAVE_FILE_NEW);
    PT_ASSERT(outcome.erased);
    PT_ASSERT(removed);
}

struct bare_name_outcome {
    int init, store;
    intmax_t size;
    bool tmp_left;
};

static void use_bare_name(void *context)
{
    struct bare_name_outcome *const outcome = context;
    set_image(0x12);
    outcome->init = save_file_init("emerald.sav");
    outcome->store = save_file_store();
    outcome->size = file_size("emerald.sav");
    outcome->tmp_left = exists("emerald.sav" TMP_SUFFIX);
    save_file_shutdown();
}

/* A path with no directory part is stored in the working directory, and
 * that is the directory fsynced. */
PTEST(a_bare_file_name_is_stored_in_the_working_directory)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    struct bare_name_outcome outcome = { 0 };
    const bool ran = in_directory(scratch.dir, use_bare_name, &outcome);
    const intmax_t size = file_size(scratch.path); /* <dir>/emerald.sav */
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(ran);
    PT_ASSERT_EQ_INT(outcome.init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(outcome.store, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(outcome.size, FILE_BYTES);
    PT_ASSERT(!outcome.tmp_left);
    PT_ASSERT_EQ_INT(size, FILE_BYTES);
    PT_ASSERT(removed);
}

PTEST_MAIN()
