/*
 * Tests of save_file.c's store that need a POSIX system: the file size limit
 * (RLIMIT_FSIZE) that makes the write to the temporary file fail part-way, as
 * a full disk would, and works for any user, root included. The rest of the
 * store's tests are in ../test_save_file_store.c. Format and conventions:
 * ../save_file_fixture.h.
 *
 * The SIGXFSZ the limit raises is ignored for the store, by signal(2) with
 * SIG_IGN, so that the write fails with an error instead of ending the process.
 */
// deps: save_file.c gba_memmap.c flash.c
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "save_file_fixture.h"

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

    struct rlimit old_limit = {0};
    const bool got_limit = getrlimit(RLIMIT_FSIZE, &old_limit) == 0;
    const struct rlimit low_limit = {HALF_CHIP_BYTES, old_limit.rlim_max};
    void (*const old_handler)(int) = signal(SIGXFSZ, SIG_IGN);
    const bool limited = got_limit && old_handler != SIG_ERR && setrlimit(RLIMIT_FSIZE, &low_limit) == 0;
    const int second = limited ? save_file_store() : SAVE_FILE_OK;
    const bool restored = got_limit && setrlimit(RLIMIT_FSIZE, &old_limit) == 0 && old_handler != SIG_ERR
                          && signal(SIGXFSZ, old_handler) != SIG_ERR;

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

/* The save is opened as every file the platform keeps is (host_file_open):
 * readable by everyone and writable by its owner whatever the umask says, as
 * host_file.h promises, where fopen would give 0666 less the umask. */
PTEST(the_save_file_is_created_the_way_every_kept_file_is)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    set_image(0x0E);
    const mode_t old_mask = umask(0);
    const int init = save_file_init(scratch.path);
    const int store = save_file_store();
    (void)umask(old_mask);
    struct stat info = {0};
    const bool statted = stat(scratch.path, &info) == 0;
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT(statted);
    PT_ASSERT_EQ_U32(info.st_mode & 0777u, 0644u);
    PT_ASSERT(removed);
}

PTEST_MAIN()
