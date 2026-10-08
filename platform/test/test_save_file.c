/*
 * Tests of save_file.c: the CRC and layout tag, init, and loading every kind
 * of file, valid or not. Format and conventions: save_file_fixture.h.
 */
// deps: gba_memmap.c flash.c
#include "save_file_fixture.h"

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

/* Seven bytes of the magic, without its terminator: no header, and too short
 * for a bare image. */
PTEST(the_magic_without_its_last_byte_is_rejected)
{
    memcpy(file_bytes, MAGIC, MAGIC_BYTES - 1);
    check_rejected_file(MAGIC_BYTES - 1);
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

PTEST_MAIN()
