/*
 * Tests of save_file.c's store: the file it writes, the atomic replacement
 * and its failures, and paths. Failures are provoked in ways that work for
 * any user, root included: a directory where the temporary file or the save
 * must go for an open or rename that fails. The write that fails part-way,
 * for which a file size limit (RLIMIT_FSIZE) is the means, is in
 * posix/test_save_file_store_posix.c. Format and conventions:
 * save_file_fixture.h.
 */
// deps: save_file.c gba_memmap.c flash.c
#include "save_file_fixture.h"

/* A refused file (an emulator's save) is never written over: the store that
 * follows the refusal reports it, writes nothing, leaves no tmp file, and the
 * file is byte for byte what it was. A later init re-arms the store. */
PTEST(the_store_never_touches_a_refused_file)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    fill_pattern(expected_file, CHIP_BYTES, 0x2A);
    const bool written = write_file(scratch.path, expected_file, CHIP_BYTES);
    flash_init();
    const int init = save_file_init(scratch.path);
    const int load = save_file_load();
    const int store = save_file_store();
    const bool tmp_made = exists(scratch.tmp);
    const bool unchanged = read_file(scratch.path, CHIP_BYTES) && memcmp(file_bytes, expected_file, CHIP_BYTES) == 0;
    const int init_again = save_file_init(scratch.path);
    const int store_again = save_file_store();
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_REFUSED);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_REFUSED);
    PT_ASSERT(!tmp_made);
    PT_ASSERT(unchanged);
    PT_ASSERT_EQ_INT(init_again, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store_again, SAVE_FILE_OK);
    PT_ASSERT(removed);
}

/* A file loaded after a refusal is not refused: the store works again once the
 * path holds a valid save. */
PTEST(the_store_works_after_a_refused_file_is_replaced_by_a_save)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    fill_pattern(expected_file, CHIP_BYTES, 0x2A);
    const bool written = write_file(scratch.path, expected_file, CHIP_BYTES);
    set_image(0x2F);
    const int init = save_file_init(scratch.path);
    const int refused = save_file_load();
    const bool removed_file = tsup_remove_file(scratch.path);
    const int missing = save_file_load();
    memcpy(flash_image(), expected_image, CHIP_BYTES);
    const int store = save_file_store();
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool image_stored = memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(refused, SAVE_FILE_REFUSED);
    PT_ASSERT(removed_file);
    PT_ASSERT_EQ_INT(missing, SAVE_FILE_NEW);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT(read);
    PT_ASSERT(image_stored);
    PT_ASSERT(removed);
}

/* The format is the same bytes at either pointer width: the header of an
 * erased chip is pinned here as literal bytes (the CRC is zlib's crc32 of
 * 131072 bytes of 0xFF), so a 32-bit build's file and a 64-bit build's
 * differ only in the pointer-width word, and each can read the other's. */
PTEST(the_header_of_an_erased_chip_is_pinned_byte_for_byte)
{
    static const uint8_t expected_header[HEADER_BYTES] = {
        'P',  'E',  'N',  'S',  'A', 'V', 'E', 0, 1, 0, 0, 0, /* version 1 */
        0,    0,    0,    0,                                  /* pointer width, filled in below */
        1,    0,    0,    0,                                  /* layout id 1 */
        0x00, 0x00, 0x02, 0x00,                               /* image size 131072 */
        0xCC, 0x03, 0x48, 0x15,                               /* CRC-32 0x154803CC */
    };
    uint8_t want[HEADER_BYTES];
    memcpy(want, expected_header, sizeof want);
    put_u32le(want + POINTER_BITS_OFFSET, native_pointer_bits());

    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    flash_init();
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool header_matches = memcmp(file_bytes, want, HEADER_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT(read);
    PT_ASSERT(header_matches);
    PT_ASSERT(removed);
}

/* A store always writes this build's tag (save_file.h): a foreign image that
 * main chose to keep is relabelled native, image bytes unchanged. */
PTEST(storing_a_foreign_image_relabels_it_as_native)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    fill_pattern(expected_image, CHIP_BYTES, 0x2E);
    build_file(other_pointer_bits(), SAVE_FILE_LAYOUT_ID);
    const bool written = write_file(scratch.path, file_bytes, FILE_BYTES);
    flash_init();
    const int init = save_file_init(scratch.path);
    const int first_load = save_file_load();
    const int store = save_file_store();
    memset(flash_image(), 0x00, CHIP_BYTES);
    const int second_load = save_file_load();
    const bool loaded = image_is_expected();
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first_load, SAVE_FILE_FOREIGN_LAYOUT);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(second_load, SAVE_FILE_OK);
    PT_ASSERT(loaded);
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
    struct save_file_layout layout = {0};
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
    PT_ASSERT_EQ_U32(get_u32le(file_bytes + CRC_OFFSET), save_file_crc32(expected_image, CHIP_BYTES));
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

/* A tmp file a crashed store left behind may be longer than the new file: the
 * store opens it truncating, so none of its tail survives the rename. */
PTEST(a_longer_stale_tmp_does_not_leave_its_tail_in_the_stored_file)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    memset(file_bytes, 0xEE, sizeof file_bytes); /* FILE_BYTES + 1 long: one more than the store writes */
    const bool stale = write_file(scratch.tmp, file_bytes, sizeof file_bytes);
    set_image(0x0C);
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    const intmax_t size = file_size(scratch.path);
    const bool tmp_left = exists(scratch.tmp);
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool image = memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(stale);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(size, FILE_BYTES);
    PT_ASSERT(!tmp_left);
    PT_ASSERT(read);
    PT_ASSERT(image);
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
    const bool made = tsup_make_directory(scratch.tmp);
    set_image(0x0F);
    const int second = save_file_store();
    fill_pattern(expected_image, CHIP_BYTES, 0x0E); /* the old save */
    const bool read = read_file(scratch.path, FILE_BYTES);
    const bool old_intact = memcmp(file_bytes + HEADER_BYTES, expected_image, CHIP_BYTES) == 0;
    const bool directory_kept = tsup_remove_directory(scratch.tmp);
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
    const bool made = tsup_make_directory(scratch.path);
    set_image(0x10);
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    const bool tmp_left = exists(scratch.tmp);
    const bool directory_kept = tsup_remove_directory(scratch.path);
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
    struct null_path_outcome outcome = {0};
    const bool ran = tsup_in_directory(scratch.dir, use_null_path, &outcome);
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
    struct bare_name_outcome outcome = {0};
    const bool ran = tsup_in_directory(scratch.dir, use_bare_name, &outcome);
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
