/*
 * test_restart.c: the arguments of a restarted process and the exec itself
 * (restart.c). The exec itself, which forks a child, is in
 * posix/test_restart_posix.c.
 */
#include "ptest.h"
#include "restart.h"

#include <string.h>

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

PTEST(leaves_out_the_audio_dump_and_its_path)
{
    char *const argv[] = {"game", "--audio-dump", "a.wav", "--silent", "--audio-dump", "b.wav"};
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(6, argv, 9, 0, &args), 0);
    PT_ASSERT_EQ_STR(args.argv[0], "game");
    PT_ASSERT_EQ_STR(args.argv[1], "--silent");
    PT_ASSERT_EQ_STR(args.argv[2], "--resume-at");
    PT_ASSERT_EQ_STR(args.argv[3], "9");
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
    char *const dump_only[] = {"game", "--audio-dump"};
    struct restart_args args;
    PT_ASSERT_EQ_INT(restart_build_args(2, resume_only, 1, 0, &args), -1);
    PT_ASSERT_EQ_INT(restart_build_args(2, epoch_only, 1, 0, &args), -1);
    PT_ASSERT_EQ_INT(restart_build_args(2, dump_only, 1, 0, &args), -1);
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

PTEST_MAIN()
