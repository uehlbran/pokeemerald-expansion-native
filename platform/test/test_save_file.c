/*
 * Tests of the save file, save_file.c. The file format is the chip image
 * byte for byte (sector s at offset s * 4096), 131072 bytes for Emerald's
 * 1 Mbit chip (GBATEK "GBA Cart Backup Flash ROM"), which is what GBA
 * emulators write for such a cartridge.
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

_Static_assert(FLASH_CHIP_SIZE == CHIP_BYTES, "chip size");

/* Whole images for expected and read-back contents; static because they are
 * 128 KiB each. */
static uint8_t expected_image[CHIP_BYTES];
static uint8_t file_bytes[CHIP_BYTES + 1];

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

/* A .sav as an emulator writes it: the chip's bytes in order. Loading it puts
 * sector s at s * 4096, banks included, and loading is not a game write. */
PTEST(an_emulator_save_loads_byte_for_byte)
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
    uint8_t sector_17[SECTOR_BYTES];
    ReadFlash(17, 0, sector_17, SECTOR_BYTES);
    const bool sector_17_matches =
        memcmp(sector_17, expected_image + 17 * SECTOR_BYTES, SECTOR_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_OK);
    PT_ASSERT(loaded);
    PT_ASSERT(sector_17_matches);
    PT_ASSERT(!dirty);
    PT_ASSERT(removed);
}

/* A file of the wrong size is reported, the chip is left erased (not partly
 * loaded, not left as it was), nothing is marked dirty, and the file stays. */
static void check_rejected_size(size_t size)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    fill_pattern(file_bytes, size, 0x05);
    const bool written = write_file(scratch.path, file_bytes, size);
    set_image(0x06);
    const int init = save_file_init(scratch.path);
    const int load = save_file_load();
    const bool erased = image_is_erased();
    const bool dirty = flash_take_dirty();
    const intmax_t size_after = file_size(scratch.path);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_CORRUPT);
    PT_ASSERT(erased);
    PT_ASSERT(!dirty);
    PT_ASSERT_EQ_INT(size_after, (intmax_t)size);
    PT_ASSERT(removed);
}

PTEST(a_512_kbit_save_is_rejected) { check_rejected_size(HALF_CHIP_BYTES); }
PTEST(a_file_one_byte_too_long_is_rejected) { check_rejected_size(CHIP_BYTES + 1); }
PTEST(a_file_one_byte_too_short_is_rejected) { check_rejected_size(CHIP_BYTES - 1); }
PTEST(an_empty_file_is_rejected) { check_rejected_size(0); }

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
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(size, CHIP_BYTES);
    PT_ASSERT(!tmp_left);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_OK);
    PT_ASSERT(round_trip);
    PT_ASSERT(removed);
}

/* The stored file is the chip's bytes in order, as an emulator expects, and
 * a game write through the driver is in it at its sector's offset. */
PTEST(stored_file_is_the_chip_byte_for_byte)
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
    const bool read = read_file(scratch.path, CHIP_BYTES);
    const bool same = memcmp(file_bytes, expected_image, CHIP_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_U32(program, FLASH_STATUS_OK);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT(read);
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
    const bool read = read_file(scratch.path, CHIP_BYTES);
    const bool newest = memcmp(file_bytes, expected_image, CHIP_BYTES) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(second, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(size, CHIP_BYTES);
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
    const bool read = read_file(scratch.path, CHIP_BYTES);
    const bool old_intact = memcmp(file_bytes, expected_image, CHIP_BYTES) == 0;
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
    const bool read = read_file(scratch.path, CHIP_BYTES);
    const bool old_intact = memcmp(file_bytes, expected_image, CHIP_BYTES) == 0;
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
    PT_ASSERT_EQ_INT(outcome.size, CHIP_BYTES);
    PT_ASSERT(!outcome.tmp_left);
    PT_ASSERT_EQ_INT(size, CHIP_BYTES);
    PT_ASSERT(removed);
}

PTEST_MAIN()
