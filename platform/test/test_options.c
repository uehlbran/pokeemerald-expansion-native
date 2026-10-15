// deps: options.c
/*
 * test_options.c: the command line (options.c): defaults, every option, the
 * limits of each number, and the combinations that are refused.
 */
#include "ptest.h"
#include "options.h"
#include "test_support.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define OPTIONS_TEST_MAX_ARGS 31

/* Parses a NULL-terminated argument list (argv[0] added), with the error
 * messages and usage text sent to the null device: tests check the result, and the
 * text is checked directly in the last test. */
static enum options_error parse(struct options *out, const char *const *args)
{
    FILE *const null_device = fopen(tsup_null_device(), "w");
    struct tsup_stderr saved_stderr;
    const bool silenced = null_device != NULL && tsup_redirect_stderr(&saved_stderr, null_device);
    char *argv[OPTIONS_TEST_MAX_ARGS + 1] = {"pokeemerald-native"};
    int argc = 1;
    for (; *args != NULL; args++) {
        if (argc > OPTIONS_TEST_MAX_ARGS) {
            (void)fprintf(stderr, "test_options: too many arguments for the helper\n");
            abort();
        }
        argv[argc++] = (char *)(uintptr_t)*args;
    }
    const enum options_error result = options_parse(argc, argv, out);
    if (silenced)
        (void)tsup_restore_stderr(&saved_stderr);
    if (null_device != NULL)
        (void)fclose(null_device);
    return result;
}

#define PARSE(out, ...) parse((out), (const char *const[]){__VA_ARGS__, NULL})

PTEST(no_arguments_give_the_documented_defaults)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, NULL), OPTIONS_OK);
    PT_ASSERT(!o.headless && !o.silent && !o.unthrottled && !o.help && !o.test_runner && !o.rtc_fixed);
    PT_ASSERT_EQ_INT(o.scale, 3); /* docs/BUILDING.md: "scale 3" */
    PT_ASSERT_EQ_U32(o.frame_limit, 0);
    PT_ASSERT_EQ_STR(o.save_path, "pokeemerald.sav");
    PT_ASSERT_EQ_STR(o.rtc_offset_path, "pokeemerald.rtc");
    PT_ASSERT_EQ_STR(o.out_dir, ".");
    PT_ASSERT(o.screenshot_path == NULL && o.replay_path == NULL && o.persistent_state == NULL);
    PT_ASSERT(o.audio_dump_path == NULL);
    PT_ASSERT_EQ_U32(o.resume_after_line, 0);
}

/* --silent's effect on the audio device (main.c: AUDIO_SILENT instead of AUDIO_DEVICE) cannot be tested
 * here, since opening the device needs SDL, and is left to the smoke gates, which run with --silent.
 * This test pins the flag the mapping reads. */
PTEST(the_flags_set_their_own_field_only)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--headless"), OPTIONS_OK);
    PT_ASSERT(o.headless && !o.silent && !o.unthrottled && !o.help && !o.test_runner);
    PT_ASSERT_EQ_INT(PARSE(&o, "--silent"), OPTIONS_OK);
    PT_ASSERT(!o.headless && o.silent && !o.unthrottled && !o.help && !o.test_runner);
    PT_ASSERT_EQ_INT(PARSE(&o, "--unthrottled"), OPTIONS_OK);
    PT_ASSERT(!o.headless && !o.silent && o.unthrottled && !o.help && !o.test_runner);
    PT_ASSERT_EQ_INT(PARSE(&o, "--help"), OPTIONS_OK);
    PT_ASSERT(!o.headless && !o.silent && !o.unthrottled && o.help && !o.test_runner);
}

PTEST(the_options_with_a_value_take_the_next_argument)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--save", "a.sav", "--rtc-offset", "a.rtc", "--screenshot", "s.ppm", "--replay",
                           "r.replay", "--out", "dir"),
                     OPTIONS_OK);
    PT_ASSERT_EQ_STR(o.save_path, "a.sav");
    PT_ASSERT_EQ_STR(o.rtc_offset_path, "a.rtc");
    PT_ASSERT_EQ_STR(o.screenshot_path, "s.ppm");
    PT_ASSERT_EQ_STR(o.replay_path, "r.replay");
    PT_ASSERT_EQ_STR(o.out_dir, "dir");
    PT_ASSERT(o.audio_dump_path == NULL);
}

PTEST(audio_dump_takes_a_path_with_or_without_a_device)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--audio-dump", "out.wav"), OPTIONS_OK);
    PT_ASSERT_EQ_STR(o.audio_dump_path, "out.wav");
    PT_ASSERT(!o.silent && !o.headless);
    PT_ASSERT_EQ_INT(PARSE(&o, "--headless", "--silent", "--audio-dump", "a b.wav"), OPTIONS_OK);
    PT_ASSERT_EQ_STR(o.audio_dump_path, "a b.wav");
    PT_ASSERT(o.silent && o.headless);
}

PTEST(a_string_option_followed_by_another_option_is_refused)
{
    struct options o;
    static const char *const with_string[] = {"--save",   "--rtc-offset", "--screenshot",
                                              "--replay", "--out",        "--audio-dump"};
    for (size_t i = 0; i < sizeof with_string / sizeof with_string[0]; i++)
        PT_ASSERT_EQ_INT(PARSE(&o, with_string[i], "--headless"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--test-runner", "--persistent-state", "--headless"), OPTIONS_ERR_USAGE);
    /* a path may still start with one dash, or "-" and a name */
    PT_ASSERT_EQ_INT(PARSE(&o, "--save", "-odd.sav"), OPTIONS_OK);
    PT_ASSERT_EQ_STR(o.save_path, "-odd.sav");
}

PTEST(an_option_with_a_value_as_the_last_argument_is_refused)
{
    struct options o;
    static const char *const with_value[] = {
        "--save",  "--rtc-offset", "--screenshot", "--replay",    "--out",
        "--scale", "--frames",     "--resume-at",  "--rtc-fixed", "--audio-dump",
    };
    for (size_t i = 0; i < sizeof with_value / sizeof with_value[0]; i++)
        PT_ASSERT_EQ_INT(PARSE(&o, with_value[i]), OPTIONS_ERR_USAGE);
    /* after --test-runner, so only the missing value is wrong */
    PT_ASSERT_EQ_INT(PARSE(&o, "--test-runner", "--persistent-state"), OPTIONS_ERR_USAGE);
}

PTEST(scale_accepts_one_to_the_maximum_and_nothing_else)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", "1"), OPTIONS_OK);
    PT_ASSERT_EQ_INT(o.scale, 1);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", "16"), OPTIONS_OK);
    PT_ASSERT_EQ_INT(o.scale, OPTIONS_MAX_SCALE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", "0"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", "17"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", "-1"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", "3x"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", ""), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", " 3"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--scale", "+3"), OPTIONS_ERR_USAGE);
}

PTEST(frames_accepts_one_to_uint32_max_and_nothing_else)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--frames", "1"), OPTIONS_OK);
    PT_ASSERT_EQ_U32(o.frame_limit, 1);
    PT_ASSERT_EQ_INT(PARSE(&o, "--frames", "4294967295"), OPTIONS_OK);
    PT_ASSERT_EQ_U32(o.frame_limit, UINT32_MAX);
    PT_ASSERT_EQ_INT(PARSE(&o, "--frames", "0"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--frames", "4294967296"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--frames", "99999999999999999999999"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--frames", "ten"), OPTIONS_ERR_USAGE);
}

PTEST(resume_at_accepts_zero_to_uint32_max)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--resume-at", "0"), OPTIONS_OK);
    PT_ASSERT_EQ_U32(o.resume_after_line, 0);
    PT_ASSERT_EQ_INT(PARSE(&o, "--resume-at", "4294967295"), OPTIONS_OK);
    PT_ASSERT_EQ_U32(o.resume_after_line, UINT32_MAX);
    PT_ASSERT_EQ_INT(PARSE(&o, "--resume-at", "4294967296"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--resume-at", "-1"), OPTIONS_ERR_USAGE);
}

PTEST(rtc_fixed_takes_a_non_negative_unix_time_up_to_int64_max)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--rtc-fixed", "0"), OPTIONS_OK);
    PT_ASSERT(o.rtc_fixed);
    PT_ASSERT_EQ_INT(o.rtc_fixed_epoch == 0, 1);
    PT_ASSERT_EQ_INT(PARSE(&o, "--rtc-fixed", "1700000000"), OPTIONS_OK);
    PT_ASSERT_EQ_INT(o.rtc_fixed_epoch == 1700000000, 1);
    PT_ASSERT_EQ_INT(PARSE(&o, "--rtc-fixed", "9223372036854775807"), OPTIONS_OK);
    PT_ASSERT_EQ_INT(o.rtc_fixed_epoch == INT64_MAX, 1);
    PT_ASSERT_EQ_INT(PARSE(&o, "--rtc-fixed", "9223372036854775808"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--rtc-fixed", "-1"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--rtc-fixed", "12abc"), OPTIONS_ERR_USAGE);
}

PTEST(without_rtc_fixed_the_clock_is_not_fixed)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--headless"), OPTIONS_OK);
    PT_ASSERT(!o.rtc_fixed);
}

PTEST(an_unknown_argument_is_refused)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--nonsense"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "stray"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--headless", "--nonsense"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--Headless"), OPTIONS_ERR_USAGE);
}

PTEST(test_runner_mode_writes_no_files)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--save", "a.sav", "--test-runner", "--rtc-offset", "a.rtc"), OPTIONS_OK);
    PT_ASSERT(o.test_runner);
    PT_ASSERT(o.save_path == NULL);
    PT_ASSERT(o.rtc_offset_path == NULL);
}

PTEST(the_persistent_state_needs_test_runner_mode)
{
    struct options o;
    PT_ASSERT_EQ_INT(PARSE(&o, "--persistent-state", "00ff"), OPTIONS_ERR_USAGE);
    PT_ASSERT_EQ_INT(PARSE(&o, "--test-runner", "--persistent-state", "00ff"), OPTIONS_OK);
    PT_ASSERT_EQ_STR(o.persistent_state, "00ff");
    PT_ASSERT_EQ_INT(PARSE(&o, "--persistent-state", "00ff", "--test-runner"), OPTIONS_OK);
}

PTEST(argc_and_argv_are_kept_for_the_crash_reboot)
{
    struct options o;
    char *argv[] = {"game", "--test-runner", "--headless"};
    PT_ASSERT_EQ_INT(options_parse(3, argv, &o), OPTIONS_OK);
    PT_ASSERT_EQ_INT(o.argc, 3);
    PT_ASSERT(o.argv == argv);
}

/* Room for the usage text, which is about 2 KiB. */
#define USAGE_TEXT_SIZE 8192u

PTEST(the_usage_text_names_every_option_and_the_defaults)
{
    static char text[USAGE_TEXT_SIZE];
    FILE *file = tsup_open_temp_stream();
    PT_ASSERT(file != NULL);
    options_print_usage(file);
    const bool rewound = fseek(file, 0, SEEK_SET) == 0;
    const size_t length = rewound ? fread(text, 1, sizeof text - 1u, file) : 0;
    text[length] = '\0';
    (void)fclose(file);
    PT_ASSERT(rewound);
    PT_ASSERT(length > 0 && length < sizeof text - 1u);
    static const char *const expected[] = {
        "--headless",   "--silent",     "--save",        "--rtc-offset",       "--scale",         "--unthrottled",
        "--frames",     "--screenshot", "--test-runner", "--persistent-state", "--replay",        "--out",
        "--rtc-fixed",  "--resume-at",  "--help",        "pokeemerald.sav",    "pokeemerald.rtc", "1 to 16 (default 3)",
        "--audio-dump",
    };
    bool all_present = true;
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++)
        all_present = all_present && strstr(text, expected[i]) != NULL;
    PT_ASSERT(all_present);
}

PTEST_MAIN()
