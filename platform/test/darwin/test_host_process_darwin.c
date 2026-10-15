/*
 * test_host_process_darwin.c: the executable's path (host_process.h) on
 * macOS, which has no /proc/self/exe: the path dyld recorded at the start,
 * _NSGetExecutablePath's, absolute. The restart that execs it is
 * ../posix/test_host_process_posix.c's.
 */
#include <mach-o/dyld.h>
#include <stdint.h>
#include <string.h>

#include "host_process.h"
#include "ptest.h"

PTEST(the_path_is_dyld_s_and_absolute)
{
    char path[HOST_PROCESS_PATH_SIZE];
    char dyld_path[HOST_PROCESS_PATH_SIZE];
    uint32_t size = (uint32_t)sizeof dyld_path;
    PT_ASSERT_EQ_INT(host_process_executable_path(path, sizeof path), 0);
    PT_ASSERT_EQ_INT(_NSGetExecutablePath(dyld_path, &size), 0);
    PT_ASSERT(strcmp(path, dyld_path) == 0);
    PT_ASSERT(path[0] == '/');
}

PTEST(a_buffer_too_small_for_the_path_is_refused)
{
    char path[HOST_PROCESS_PATH_SIZE];
    PT_ASSERT_EQ_INT(host_process_executable_path(path, sizeof path), 0);
    const size_t length = strlen(path);
    char small[HOST_PROCESS_PATH_SIZE];
    PT_ASSERT_EQ_INT(host_process_executable_path(small, length), -1); /* no room for the NUL */
    PT_ASSERT_EQ_INT(host_process_executable_path(small, length + 1u), 0);
}

PTEST_MAIN()
