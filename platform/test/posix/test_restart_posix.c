/*
 * test_restart_posix.c: the exec of a restart (restart.c). It is tested by
 * running this test binary again in a forked child: the constructor notices
 * RESTART_TEST_CHILD_FILE in the environment, writes the arguments it was
 * started with to that file and exits before any test runs. The arguments'
 * building is in ../test_restart.c.
 */
// deps: restart.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "posix_child.h"
#include "posix_support.h"
#include "ptest.h"
#include "restart.h"
#include "test_support.h"

#define CHILD_FILE_VARIABLE "RESTART_TEST_CHILD_FILE"

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
    (void)fclose(file);
    _exit(0);
}

/* The forked child (psup_run_child): the environment's marker, then the exec. */
static void exec_in_child(void *context)
{
    char *const argv[] = {"child-name", "--one", "two"};
    struct restart_args args;
    if (tsup_set_environment(CHILD_FILE_VARIABLE, context) && restart_build_args(3, argv, 9, 0, &args) == 0)
        (void)restart_exec(&args);
    _exit(3);
}

PTEST(exec_runs_this_executable_again_with_the_arguments)
{
    char path[TSUP_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_file(path, sizeof path, "test_restart"));
    const int status = psup_run_child(exec_in_child, path);
    char content[256] = "";
    FILE *file = fopen(path, "r");
    size_t length = file != NULL ? fread(content, 1, sizeof content - 1, file) : 0;
    if (file != NULL)
        (void)fclose(file);
    content[length] = '\0';
    PT_ASSERT(tsup_remove_file(path));
    PT_ASSERT(status != -1);
    PT_ASSERT_EQ_INT(psup_exit_code(status), 0);
    PT_ASSERT_EQ_STR(content, "child-name\n--one\ntwo\n--resume-at\n9\n");
}

PTEST_MAIN()
