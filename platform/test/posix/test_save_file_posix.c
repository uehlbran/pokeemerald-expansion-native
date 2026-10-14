/*
 * Tests of save_file.c's load that need a POSIX system: a file whose
 * permissions forbid reading it (chmod, access). The rest of the load's tests
 * are in ../test_save_file.c. Format and conventions: ../save_file_fixture.h.
 */
// deps: save_file.c gba_memmap.c flash.c
#include <sys/stat.h>
#include <unistd.h>

#include "save_file_fixture.h"

/* A file the process may not read: the open fails with something other than
 * ENOENT, which is corrupt (reported), not a new game. Root can read it, so
 * the expectation follows access(). */
PTEST(a_file_that_cannot_be_opened_is_rejected_not_treated_as_new)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    fill_pattern(expected_image, CHIP_BYTES, 0x2D);
    const bool written = write_file(scratch.path, expected_image, CHIP_BYTES);
    const bool locked = chmod(scratch.path, 0) == 0;
    const bool readable = access(scratch.path, R_OK) == 0;
    set_image(0x08);
    const int init = save_file_init(scratch.path);
    const int load = save_file_load();
    const bool erased = image_is_erased();
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT(locked);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, readable ? SAVE_FILE_LEGACY : SAVE_FILE_CORRUPT);
    PT_ASSERT(readable || erased);
    PT_ASSERT(removed);
}

PTEST_MAIN()
