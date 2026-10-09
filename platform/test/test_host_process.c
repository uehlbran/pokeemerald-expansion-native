/*
 * test_host_process.c: the running executable's file, and starting it again
 * (host_process.h).
 *
 * The path is checked by what it names: a file that holds this binary's own
 * marker string. The restart runs this very binary again: its constructor
 * notices CHILD_FILE_VARIABLE in the environment (which the restart must
 * pass on), writes the arguments it was started with to that file and exits
 * before any test runs.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host_process.h"
#include "ptest.h"

#define CHILD_FILE_VARIABLE "HOST_PROCESS_TEST_CHILD_FILE"
#define SELF_NAME "test_host_process"
#define CHILD_EXEC_FAILED 3

/* In this binary's read-only data, so in its file. */
__attribute__((used)) static const char marker[] = "test_host_process: the marker of this executable's own file";

__attribute__((constructor)) static void act_as_the_restarted_process(int argc, char **argv)
{
    const char *path = getenv(CHILD_FILE_VARIABLE);
    if (path == NULL)
        return;
    FILE *file = fopen(path, "w");
    if (file == NULL)
        _exit(2);
    for (int i = 0; i < argc; i++)
        (void)fprintf(file, "%s\n", argv[i]);
    _exit(fclose(file) == 0 ? 0 : 2);
}

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

PTEST(restart_runs_this_executable_again_with_the_arguments_and_the_environment)
{
    char file_path[64];
    (void)snprintf(file_path, sizeof file_path, "/tmp/test_host_process_XXXXXX");
    const int fd = mkstemp(file_path);
    PT_ASSERT(fd >= 0);
    PT_ASSERT_EQ_INT(close(fd), 0);
    (void)fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        static char first[] = "first", second[] = "--second", third[] = "third";
        char *const argv[] = {first, second, third, NULL};
        if (setenv(CHILD_FILE_VARIABLE, file_path, 1) == 0)
            (void)host_process_restart(argv);
        _exit(CHILD_EXEC_FAILED);
    }
    int status = -1;
    const bool waited = pid > 0 && waitpid(pid, &status, 0) == pid;
    char written[64] = "";
    FILE *file = fopen(file_path, "r");
    if (file != NULL) {
        written[fread(written, 1, sizeof written - 1u, file)] = '\0';
        (void)fclose(file);
    }
    (void)unlink(file_path);
    PT_ASSERT(waited);
    PT_ASSERT(WIFEXITED(status));
    PT_ASSERT_EQ_INT(WEXITSTATUS(status), 0);
    PT_ASSERT_EQ_STR(written, "first\n--second\nthird\n");
}

PTEST_MAIN()
