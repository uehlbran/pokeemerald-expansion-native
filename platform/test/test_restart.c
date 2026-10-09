/*
 * test_restart.c: the arguments of a restarted process and the exec itself
 * (restart.c). The exec is tested by running this test binary again: its
 * constructor notices RESTART_TEST_CHILD_FILE in the environment, writes the
 * arguments it was started with to that file and exits before any test runs.
 */
#include "ptest.h"
#include "restart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
        fprintf(file, "%s\n", argv[i]);
    fclose(file);
    _exit(0);
}

static char *const plain[] = {"game", "--headless", "--save", "a.sav"};

PTEST(appends_resume_at_to_the_original_arguments)
{
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(4, plain, 17, 0, &args), 0);
    PT_ASSERT_EQ_STR(args.argv[0], "game");
    PT_ASSERT_EQ_STR(args.argv[3], "a.sav");
    PT_ASSERT_EQ_STR(args.argv[4], "--resume-at");
    PT_ASSERT_EQ_STR(args.argv[5], "17");
    PT_ASSERT(args.argv[6] == NULL);
}

PTEST(replaces_an_earlier_resume_at_and_its_value)
{
    char *const argv[] = {"game", "--resume-at", "5", "--silent"};
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(4, argv, 40, 0, &args), 0);
    PT_ASSERT_EQ_STR(args.argv[1], "--silent");
    PT_ASSERT_EQ_STR(args.argv[2], "--resume-at");
    PT_ASSERT_EQ_STR(args.argv[3], "40");
    PT_ASSERT(args.argv[4] == NULL);
}

PTEST(advances_the_fixed_clock)
{
    char *const argv[] = {"game", "--rtc-fixed", "1700000000", "--silent"};
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(4, argv, 3, 1700000123, &args), 0);
    PT_ASSERT_EQ_STR(args.argv[1], "--rtc-fixed");
    PT_ASSERT_EQ_STR(args.argv[2], "1700000123");
    PT_ASSERT_EQ_STR(args.argv[3], "--silent");
    PT_ASSERT_EQ_STR(args.argv[4], "--resume-at");
}

PTEST(without_a_fixed_clock_the_epoch_is_ignored)
{
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(4, plain, 3, 99, &args), 0);
    for (size_t i = 0; args.argv[i] != NULL; i++)
        PT_ASSERT(strcmp(args.argv[i], "99") != 0);
}

PTEST(an_option_without_its_value_is_an_error)
{
    char *const resume_only[] = {"game", "--resume-at"};
    char *const epoch_only[] = {"game", "--rtc-fixed"};
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(2, resume_only, 1, 0, &args), -1);
    PT_ASSERT_EQ_INT(restart_build_args(2, epoch_only, 1, 0, &args), -1);
}

PTEST(too_many_arguments_are_an_error)
{
    char *many[RESTART_MAX_ARGS + 2];
    struct restart_args args;
    for (size_t i = 0; i < RESTART_MAX_ARGS + 2; i++)
        many[i] = "x";
    PT_ASSERT_EQ_INT(restart_build_args((int)RESTART_MAX_ARGS - 1, many, 1, 0, &args),
                     -1); /* no room for the two added */
    PT_ASSERT_EQ_INT(restart_build_args((int)RESTART_MAX_ARGS - 2, many, 1, 0, &args), 0);
    PT_ASSERT_EQ_INT(restart_build_args((int)RESTART_MAX_ARGS + 1, many, 1, 0, &args), -1);
}

PTEST(a_large_epoch_and_line_fit_their_buffers)
{
    char *const argv[] = {"game", "--rtc-fixed", "1"};
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(3, argv, UINT32_MAX, INT64_MIN, &args), 0);
    PT_ASSERT_EQ_STR(args.argv[2], "-9223372036854775808");
    PT_ASSERT_EQ_STR(args.argv[4], "4294967295");
}

PTEST(exec_runs_this_executable_again_with_the_arguments)
{
    char path[] = "/tmp/test_restart_XXXXXX";
    int fd = mkstemp(path);
    PT_ASSERT(fd >= 0);
    close(fd);
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        char *const argv[] = {"child-name", "--one", "two"};
        struct restart_args args;
        setenv(CHILD_FILE_VARIABLE, path, 1);
        if (restart_build_args(3, argv, 9, 0, &args) == 0)
            (void)restart_exec(&args);
        _exit(3);
    }
    int status = 0;
    PT_ASSERT(pid > 0 && waitpid(pid, &status, 0) == pid);
    char content[256] = "";
    FILE *file = fopen(path, "r");
    size_t length = file != NULL ? fread(content, 1, sizeof content - 1, file) : 0;
    if (file != NULL)
        fclose(file);
    content[length] = '\0';
    unlink(path);
    PT_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    PT_ASSERT_EQ_STR(content, "child-name\n--one\ntwo\n--resume-at\n9\n");
}

PTEST_MAIN()
