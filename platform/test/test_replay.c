// deps: replay_parse.c
/*
 * test_replay.c: the replay driver (replay.c) against a fake host (the parser has
 * test_replay_parse.c). The driver's frame arithmetic is checked against
 * the contract in replay.h (replay_step call n is "frame n"; keys set by
 * call n are what the game reads in frame n + 1).
 */
#include "ptest.h"
#include "replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "input.h"

static struct replay_script script;

static int parse(const char *text, struct replay_parse_error *error)
{
    return replay_parse(text, &script, error);
}

/* --- The driver ------------------------------------------------------------ */

#define FAKE_FACTS 3
#define FAKE_KEY_LOG 512

struct fake {
    char names[FAKE_FACTS][16];
    char values[FAKE_FACTS][REPLAY_FACT_BUFFER_SIZE];
    uint16_t keys_at[FAKE_KEY_LOG]; /* keys as of each step, by step number - 1 */
    size_t steps;
    uint16_t keys;
    int screenshots;
    char last_screenshot[REPLAY_PATH_SIZE];
    int screenshot_result;
};

static int fake_get_fact(void *context, const char *name, char *out, size_t size)
{
    const struct fake *fake = context;
    for (size_t i = 0; i < FAKE_FACTS; i++) {
        if (strcmp(fake->names[i], name) == 0) {
            (void)snprintf(out, size, "%s", fake->values[i]);
            return 0;
        }
    }
    return -1;
}

static const char *fake_fact_name(void *context, size_t index)
{
    struct fake *fake = context;
    return index < FAKE_FACTS ? fake->names[index] : NULL;
}

static int fake_screenshot(void *context, const char *path)
{
    struct fake *fake = context;
    fake->screenshots++;
    (void)snprintf(fake->last_screenshot, sizeof fake->last_screenshot, "%s", path);
    return fake->screenshot_result;
}

static void fake_set_keys(void *context, uint16_t keys)
{
    struct fake *fake = context;
    fake->keys = keys;
}

static void fake_init(struct fake *fake)
{
    *fake = (struct fake){0};
    strcpy(fake->names[0], "alpha");
    strcpy(fake->names[1], "beta");
    strcpy(fake->names[2], "gamma");
    strcpy(fake->values[0], "one");
    strcpy(fake->values[1], "two");
    strcpy(fake->values[2], "three");
}

static struct replay_host host_for(struct fake *fake)
{
    return (struct replay_host){.context = fake,
                                .get_fact = fake_get_fact,
                                .fact_name = fake_fact_name,
                                .screenshot = fake_screenshot,
                                .set_keys = fake_set_keys};
}

/* Steps until the status is not RUNNING or `limit` steps passed, recording
 * the keys after each step. */
static enum replay_status run(struct replay *replay, struct fake *fake, size_t limit)
{
    enum replay_status status = REPLAY_RUNNING;
    while (status == REPLAY_RUNNING && fake->steps < limit) {
        status = replay_step(replay);
        if (fake->steps < FAKE_KEY_LOG)
            fake->keys_at[fake->steps] = fake->keys;
        fake->steps++;
    }
    return status;
}

static int start(struct replay *replay, struct fake *fake, const char *text, uint32_t resume_after)
{
    struct replay_parse_error error;
    fake_init(fake);
    if (parse(text, &error) != 0)
        return -2;
    struct replay_host host = host_for(fake);
    return replay_start(replay, &script, &host, ".", resume_after);
}

PTEST(an_empty_script_is_done_at_the_first_step)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "", 0), 0);
    PT_ASSERT_EQ_INT(replay_step(&replay), REPLAY_DONE);
    PT_ASSERT_EQ_INT(replay_step(&replay), REPLAY_DONE);
}

PTEST(press_times_n_is_n_separate_presses_then_the_next_command)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "press A times 3\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 30), REPLAY_RESTART);
    /* down, up, down, up, down, up, then the restart at step 7 */
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[1], 0);
    PT_ASSERT_EQ_U32(fake.keys_at[2], INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[3], 0);
    PT_ASSERT_EQ_U32(fake.keys_at[4], INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[5], 0);
    PT_ASSERT(fake.steps == 7);
}

PTEST(press_times_counts_again_for_a_second_such_command)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "press A times 2\npress B times 2\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 30), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys_at[2], INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[3], 0);
    PT_ASSERT_EQ_U32(fake.keys_at[4], INPUT_KEY_B);
    PT_ASSERT_EQ_U32(fake.keys_at[6], INPUT_KEY_B);
    PT_ASSERT_EQ_U32(fake.keys_at[7], 0);
    PT_ASSERT(fake.steps == 9);
}

PTEST(press_times_resumes_after_a_restart_line_without_repeating)
{
    struct replay replay;
    struct fake fake;
    /* a restart on line 2; resuming after it plays line 3 only */
    PT_ASSERT_EQ_INT(start(&replay, &fake, "press A times 2\nrestart\npress B times 2\nrestart", 2), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 30), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_B);
    PT_ASSERT_EQ_U32(fake.keys_at[2], INPUT_KEY_B);
    PT_ASSERT(fake.steps == 5);
    PT_ASSERT_EQ_INT(replay_restart_line(&replay), 4);
}

PTEST(press_is_down_for_one_frame_and_up_for_one)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "press A\npress A\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 20), REPLAY_RESTART);
    /* step 1 sets A, step 2 releases, step 3 sets again, step 4 releases and
     * the restart runs at step 5 */
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[1], 0);
    PT_ASSERT_EQ_U32(fake.keys_at[2], INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[3], 0);
    PT_ASSERT(fake.steps == 5);
    PT_ASSERT_EQ_INT(replay_restart_line(&replay), 3);
}

PTEST(wait_and_frame_count_steps)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait 3\nframe 10\nwait 0\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    /* wait 3 runs at step 1 and ends at step 4; frame 10 ends at step 10, where the rest runs */
    PT_ASSERT(fake.steps == 10);
    PT_ASSERT_EQ_INT(replay_ticks(&replay), 10);
}

PTEST(frame_in_the_past_fails_with_its_line)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait 5\n\nframe 3", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "line 3: frame 3 is in the past") != NULL);
}

PTEST(frame_equal_to_now_does_not_wait)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "frame 1\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT(fake.steps == 1);
}

PTEST(hold_and_release_persist_across_waits)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "hold UP,A\nwait 2\nrelease A\nwait 2\nrelease UP\nwait 1\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_UP | INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[1], INPUT_KEY_UP | INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[2], INPUT_KEY_UP);
    PT_ASSERT_EQ_U32(fake.keys_at[3], INPUT_KEY_UP);
    PT_ASSERT_EQ_U32(fake.keys_at[4], 0);
}

PTEST(release_of_a_key_that_was_never_held_changes_nothing)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "hold UP\nrelease A,L\nwait 2\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_UP);
    PT_ASSERT_EQ_U32(fake.keys_at[1], INPUT_KEY_UP);
}

PTEST(hold_for_releases_after_n_frames_and_keeps_other_holds)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "hold R\nhold A for 3\nwait 1\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_R | INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[1], INPUT_KEY_R | INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[2], INPUT_KEY_R | INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[3], INPUT_KEY_R);
}

PTEST(press_of_a_held_key_returns_to_held)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "hold A\npress B\nwait 1\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_A | INPUT_KEY_B);
    PT_ASSERT_EQ_U32(fake.keys_at[1], INPUT_KEY_A);
}

PTEST(wait_until_continues_in_the_frame_the_fact_changes)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until alpha=go max 50\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 4), REPLAY_RUNNING);
    strcpy(fake.values[0], "go");
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT(fake.steps == 5);
}

PTEST(wait_until_that_already_holds_does_not_wait)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until alpha=one max 5\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT(fake.steps == 1);
}

PTEST(wait_until_times_out_after_exactly_max_frames)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait 2\nwait-until alpha=never max 7\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    /* the wait-until starts at step 3; it may still succeed at step 3 + 7 and fails there */
    PT_ASSERT(fake.steps == 10);
    PT_ASSERT(strstr(replay_message(&replay), "line 2: wait-until alpha=never timed out after 7 frames") != NULL);
    PT_ASSERT(strstr(replay_message(&replay), "alpha=one") != NULL);
}

PTEST(wait_until_succeeds_in_the_last_allowed_frame)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until alpha=go max 3\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 3), REPLAY_RUNNING);
    strcpy(fake.values[0], "go");
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT(fake.steps == 4);
}

PTEST(wait_until_max_zero_requires_the_fact_now)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until alpha=go max 0", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT(fake.steps == 1);
}

PTEST(mash_presses_in_cycles_until_the_fact_holds_then_releases)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "mash A until alpha=go max 100\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 12), REPLAY_RUNNING);
    for (size_t step = 0; step < 12; step++) {
        uint16_t expected = step % (REPLAY_MASH_DOWN_FRAMES + REPLAY_MASH_UP_FRAMES) < REPLAY_MASH_DOWN_FRAMES
                                ? INPUT_KEY_A
                                : 0;
        PT_ASSERT_EQ_U32(fake.keys_at[step], expected);
    }
    strcpy(fake.values[0], "go");
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT(fake.steps == 14); /* the keys stay up one frame after the mash */
    PT_ASSERT_EQ_U32(fake.keys, 0);
}

PTEST(mash_that_already_holds_presses_nothing)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "mash A until alpha=one max 100\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT(fake.steps == 1);
    PT_ASSERT_EQ_U32(fake.keys_at[0], 0);
}

PTEST(mash_times_out_and_keeps_held_keys_while_running)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "hold UP\nmash A until alpha=never max 9\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_UP | INPUT_KEY_A);
    PT_ASSERT_EQ_U32(fake.keys_at[REPLAY_MASH_DOWN_FRAMES], INPUT_KEY_UP);
    PT_ASSERT(fake.steps == 10);
    PT_ASSERT(strstr(replay_message(&replay), "line 2: mash until alpha=never timed out after 9 frames") != NULL);
    PT_ASSERT_EQ_U32(fake.keys, 0);
}

PTEST(a_press_after_a_mash_releases_on_its_own_schedule)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "mash A until alpha=one max 9\npress B\nwait 1\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys_at[0], INPUT_KEY_B);
    PT_ASSERT_EQ_U32(fake.keys_at[1], 0);
}

PTEST(a_press_right_after_a_mash_is_a_new_press)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "mash A until alpha=go max 100\npress A\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 3), REPLAY_RUNNING); /* A is down at step 1 */
    strcpy(fake.values[0], "go");
    PT_ASSERT_EQ_INT(run(&replay, &fake, 4), REPLAY_RUNNING); /* step 4: the mash ends, keys up */
    PT_ASSERT_EQ_U32(fake.keys_at[3], 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 5), REPLAY_RUNNING); /* step 5: the press goes down */
    PT_ASSERT_EQ_U32(fake.keys_at[4], INPUT_KEY_A);
}

PTEST(wait_until_and_expect_report_an_unknown_fact)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until nope=1 max 3", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "line 1: wait-until: no such fact 'nope'") != NULL);
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait 1\nexpect nope=1", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "line 2: expect: no such fact 'nope'") != NULL);
}

PTEST(expect_passes_and_fails_with_both_values)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "expect beta=two\nexpect gamma=three\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait 1\nexpect beta=2", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "line 2: expect beta=2: the game has beta=two") != NULL);
}

PTEST(a_wait_until_that_fails_while_waiting_sees_the_fact_vanish)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until alpha=go max 50", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 2), REPLAY_RUNNING);
    fake.names[0][0] = 'x'; /* the fact stops existing */
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "no such fact") != NULL);
}

PTEST(a_failed_run_stays_failed_and_releases_the_keys)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "hold A\nwait 1\nexpect alpha=bad\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT_EQ_U32(fake.keys, 0);
    PT_ASSERT_EQ_INT(replay_step(&replay), REPLAY_FAILED);
    PT_ASSERT_EQ_INT(replay_step(&replay), REPLAY_FAILED);
}

PTEST(resume_skips_commands_up_to_the_restart_line)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(
        start(&replay, &fake, "expect alpha=bad\n# c\nrestart\nexpect alpha=one\nwait 2\nexpect beta=two", 3), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_DONE);
    PT_ASSERT(fake.steps == 3);
    PT_ASSERT_EQ_INT(start(&replay, &fake, "restart\nrestart\nwait 1", 99), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_DONE);
    PT_ASSERT(fake.steps == 1);
}

PTEST(restart_stops_the_keys_and_the_run)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "hold A\nwait 2\nrestart\nexpect alpha=bad", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_U32(fake.keys, 0);
    PT_ASSERT_EQ_INT(replay_step(&replay), REPLAY_RESTART);
}

/* Output files go to a directory of the test's own. */
static char out_dir[128];

static void make_out_dir(void)
{
    (void)snprintf(out_dir, sizeof out_dir, "/tmp/test_replay_XXXXXX");
    PT_ASSERT(mkdtemp(out_dir) != NULL);
}

static void remove_out_dir(void)
{
    char path[REPLAY_PATH_SIZE];
    (void)snprintf(path, sizeof path, "%s/d1.txt", out_dir);
    (void)unlink(path);
    (void)rmdir(out_dir);
}

PTEST(dump_writes_every_fact_and_screenshot_names_the_path)
{
    struct replay replay;
    struct fake fake;
    struct replay_parse_error error;
    char path[REPLAY_PATH_SIZE];
    char content[256] = "";
    make_out_dir();
    fake_init(&fake);
    PT_ASSERT_EQ_INT(parse("dump d1\nscreenshot s1\nrestart", &error), 0);
    struct replay_host host = host_for(&fake);
    PT_ASSERT_EQ_INT(replay_start(&replay, &script, &host, out_dir, 0), 0);
    enum replay_status status = run(&replay, &fake, 10);
    (void)snprintf(path, sizeof path, "%s/d1.txt", out_dir);
    FILE *file = fopen(path, "r");
    size_t length = file != NULL ? fread(content, 1, sizeof content - 1, file) : 0;
    if (file != NULL)
        (void)fclose(file);
    content[length] = '\0';
    remove_out_dir();
    PT_ASSERT_EQ_INT(status, REPLAY_RESTART);
    PT_ASSERT_EQ_STR(content, "alpha=one\nbeta=two\ngamma=three\n");
    PT_ASSERT_EQ_INT(fake.screenshots, 1);
    (void)snprintf(path, sizeof path, "%s/s1.ppm", out_dir);
    PT_ASSERT_EQ_STR(fake.last_screenshot, path);
}

PTEST(dump_into_a_missing_directory_fails_with_its_line)
{
    struct replay replay;
    struct fake fake;
    struct replay_parse_error error;
    fake_init(&fake);
    PT_ASSERT_EQ_INT(parse("wait 1\ndump d1", &error), 0);
    struct replay_host host = host_for(&fake);
    PT_ASSERT_EQ_INT(replay_start(&replay, &script, &host, "/nonexistent-dir-for-test", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 10), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "line 2: dump: could not open") != NULL);
}

PTEST(a_failing_screenshot_is_reported)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "screenshot s", 0), 0);
    fake.screenshot_result = -1;
    PT_ASSERT_EQ_INT(run(&replay, &fake, 10), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "line 1: screenshot: could not write") != NULL);
}

PTEST(a_too_long_output_path_is_reported)
{
    struct replay replay;
    struct fake fake;
    struct replay_parse_error error;
    char long_dir[REPLAY_PATH_SIZE - 1];
    memset(long_dir, 'd', sizeof long_dir);
    long_dir[sizeof long_dir - 1] = '\0';
    fake_init(&fake);
    PT_ASSERT_EQ_INT(parse("screenshot some_name", &error), 0);
    struct replay_host host = host_for(&fake);
    PT_ASSERT_EQ_INT(replay_start(&replay, &script, &host, long_dir, 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 10), REPLAY_FAILED);
    PT_ASSERT(strstr(replay_message(&replay), "too long") != NULL);
    PT_ASSERT_EQ_INT(fake.screenshots, 0);
}

PTEST(start_rejects_an_incomplete_host_and_a_huge_directory)
{
    struct replay replay;
    struct replay_script empty = {.count = 0};
    struct replay_host host = {0};
    PT_ASSERT_EQ_INT(replay_start(&replay, &empty, &host, ".", 0), -1);
    PT_ASSERT_EQ_INT(replay_step(&replay), REPLAY_FAILED);
    struct fake fake;
    fake_init(&fake);
    host = host_for(&fake);
    char huge[REPLAY_PATH_SIZE + 1];
    memset(huge, 'x', sizeof huge);
    huge[REPLAY_PATH_SIZE] = '\0';
    PT_ASSERT_EQ_INT(replay_start(&replay, &empty, &host, huge, 0), -1);
    PT_ASSERT_EQ_INT(replay_start(&replay, &empty, &host, NULL, 0), -1);
    PT_ASSERT_EQ_INT(replay_start(&replay, &empty, NULL, ".", 0), -1);
}

PTEST_MAIN()
