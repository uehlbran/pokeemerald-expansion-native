/*
 * test_host_process.c: the running executable's file, and starting it again
 * (host_process.h).
 *
 * The path is checked by what it names: a file that holds this binary's own
 * marker string. The restart itself, which forks, is in
 * posix/test_host_process_posix.c.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_process.h"
#include "ptest.h"

#define SELF_NAME "test_host_process"

/* In this binary's read-only data, so in its file. */
__attribute__((used)) static const char marker[] = "test_host_process: the marker of this executable's own file";

/* True if the file at path holds the marker. */
static bool file_holds_marker(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    bool found = false;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    char *bytes = size > 0 && fseek(file, 0, SEEK_SET) == 0 ? malloc((size_t)size) : NULL;
    if (bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size) {
        for (size_t i = 0; !found && i + sizeof marker <= (size_t)size; i++)
            found = memcmp(bytes + i, marker, sizeof marker) == 0;
    }
    free(bytes);
    (void)fclose(file);
    return found;
}

PTEST(the_path_names_this_executables_file)
{
    char path[HOST_PROCESS_PATH_SIZE];
    PT_ASSERT_EQ_INT(host_process_executable_path(path, sizeof path), 0);
    const size_t length = strlen(path);
    PT_ASSERT(length >= sizeof SELF_NAME - 1u);
    PT_ASSERT_EQ_STR(path + length - (sizeof SELF_NAME - 1u), SELF_NAME);
    PT_ASSERT(file_holds_marker(path));
}

PTEST(a_buffer_without_room_for_the_terminator_is_refused)
{
    char path[HOST_PROCESS_PATH_SIZE];
    char exact[HOST_PROCESS_PATH_SIZE];
    PT_ASSERT_EQ_INT(host_process_executable_path(path, sizeof path), 0);
    const size_t length = strlen(path);
    PT_ASSERT_EQ_INT(host_process_executable_path(exact, length), -1);
    PT_ASSERT_EQ_INT(host_process_executable_path(exact, length + 1u), 0);
    PT_ASSERT_EQ_STR(exact, path);
}

PTEST_MAIN()
