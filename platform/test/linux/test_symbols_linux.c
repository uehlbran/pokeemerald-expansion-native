/*
 * test_symbols_linux.c: the two failures of symbols.c's reader that a plain
 * temporary file cannot provoke, with files only Linux has: a sysfs attribute,
 * whose size is a page whatever it holds, for a file that reads shorter than
 * its size says; and a sparse file for the size limit. The rest of the
 * reader's tests are in ../test_symbols.c.
 *
 * The limit is a size of UINT32_MAX or more, so a file one byte below it would
 * be read whole (4 GiB of zeros): only the first size refused is tested, and
 * the accepted side of the boundary is not.
 */
// deps: symbols.c
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "ptest.h"
#include "symbols.h"
#include "test_support.h"

/* An attribute of every Linux machine: stat and seek say 4096 bytes, a read ends after "0-7\n" or so. */
#define SHORT_READING_FILE "/sys/devices/system/cpu/online"
#define MESSAGE_SIZE 512u

// cppcheck-suppress constParameterCallback ; tsup_capture_stderr's action takes a non-const void *
static void load_the_map(void *context)
{
    PT_ASSERT_EQ_INT(symbols_load(context), -1);
}

PTEST(a_map_that_reads_shorter_than_its_size_is_reported_by_the_fread_call)
{
    static char path[] = SHORT_READING_FILE;
    char message[MESSAGE_SIZE];
    PT_ASSERT(tsup_capture_stderr(load_the_map, path, message, sizeof message));
    PT_ASSERT_EQ_STR(message, "symbols: fread(" SHORT_READING_FILE "): Input/output error\n");
}

PTEST(a_map_of_the_smallest_refused_size_is_refused_naming_the_file_and_its_size)
{
    char path[TSUP_PATH_SIZE];
    char message[MESSAGE_SIZE];
    PT_ASSERT(tsup_make_temp_file(path, sizeof path, "test_symbols_linux"));
    const int truncated = truncate(path, (off_t)UINT32_MAX); /* sparse: no disk or memory is used */
    const bool captured = tsup_capture_stderr(load_the_map, path, message, sizeof message);
    (void)tsup_remove_file(path);
    PT_ASSERT_EQ_INT(truncated, 0);
    PT_ASSERT(captured);
    /* Where long is 32 bits the size does not fit ftell: refused earlier, by that call. */
    const char *const what = sizeof(long) > 4 ? "is 4294967295 bytes" : "ftell(";
    PT_ASSERT(strncmp(message, "symbols: ", strlen("symbols: ")) == 0);
    PT_ASSERT(strstr(message, path) != NULL);
    PT_ASSERT(strstr(message, what) != NULL);
}

PTEST_MAIN()
