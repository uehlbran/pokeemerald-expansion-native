/*
 * test_host_process_posix.c: starting the executable again (host_process.h),
 * which needs a forked child. The restart runs this very binary again: its
 * constructor sees CHILD_MODE as argv[1] or CHILD_FILE_VARIABLE in the
 * environment (the restart must pass both on), writes the arguments it was
 * started with to that file and exits before any test runs. Either marker
 * alone makes it the restarted process, so a restart that loses its arguments
 * or its environment ends there rather than running the tests, and
 * restarting, again. The path of the executable is checked in
 * ../test_host_process.c.
 *
 * The constructor runs before main, in the restarted process only; it uses
 * stdio and _exit, which is fine outside a signal handler.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host_process.h"
#include "posix_child.h"
#include "posix_support.h"
#include "ptest.h"
#include "test_support.h"

#define CHILD_FILE_VARIABLE "HOST_PROCESS_TEST_CHILD_FILE"
#define CHILD_MODE "--restarted-child"

/* Exit codes of the restarted process, and of the child before it. */
#define CHILD_OK 0
#define CHILD_WRITE_FAILED 2
#define CHILD_EXEC_FAILED 3
#define CHILD_NO_ENVIRONMENT 4 /* argv says restarted, the environment lost the file */

__attribute__((constructor)) static void act_as_the_restarted_process(int argc, char **argv)
{
    const bool marked = argc > 1 && strcmp(argv[1], CHILD_MODE) == 0;
    const char *path = getenv(CHILD_FILE_VARIABLE);
    if (!marked && path == NULL)
        return;
    if (path == NULL)
        _exit(CHILD_NO_ENVIRONMENT);
    FILE *file = fopen(path, "w");
    if (file == NULL)
        _exit(CHILD_WRITE_FAILED);
    for (int i = 0; i < argc; i++)
        (void)fprintf(file, "%s\n", argv[i]);
    _exit(fclose(file) == 0 ? CHILD_OK : CHILD_WRITE_FAILED);
}

/* The forked child (psup_run_child: no core file, a time limit): the
 * environment's marker, then the restart. */
static void restart_in_child(void *context)
{
    const char *const file_path = context;
    static char first[] = "first", mode[] = CHILD_MODE, third[] = "third";
    char *const argv[] = {first, mode, third, NULL};
    if (tsup_set_environment(CHILD_FILE_VARIABLE, file_path))
        (void)host_process_restart(argv);
    _exit(CHILD_EXEC_FAILED);
}

PTEST(restart_runs_this_executable_again_with_the_arguments_and_the_environment)
{
    char file_path[TSUP_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_file(file_path, sizeof file_path, "test_host_process"));
    /* From here every path reaches the removal below. */
    char written[64] = "";
    const int status = psup_run_child(restart_in_child, file_path);
    FILE *file = fopen(file_path, "r");
    if (file != NULL) {
        written[fread(written, 1, sizeof written - 1u, file)] = '\0';
        (void)fclose(file);
    }
    const bool removed = tsup_remove_file(file_path);
    PT_ASSERT(status != -1);
    PT_ASSERT_EQ_INT(psup_exit_code(status), CHILD_OK);
    PT_ASSERT_EQ_STR(written, "first\n" CHILD_MODE "\nthird\n");
    PT_ASSERT(removed);
}

PTEST_MAIN()
