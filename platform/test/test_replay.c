// deps: replay_parse.c
/*
 * test_replay.c: the replay script parser (replay_parse.c) and the driver
 * (replay.c) against a fake host. Every command is parsed in its good form
 * and each malformed form; the driver's frame arithmetic is checked against
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

static void assert_parse_error(const char *text, uint32_t line, const char *needle)
{
    struct replay_parse_error error = { 0 };
    int result = parse(text, &error);
    PT_ASSERT(result == -1);
    PT_ASSERT_EQ_INT(error.line, line);
    PT_ASSERT(strstr(error.message, needle) != NULL);
}

static const struct replay_command *only_command(const char *text)
{
    struct replay_parse_error error;
    if (parse(text, &error) != 0 || script.count != 1)
        return NULL;
    return &script.commands[0];
}

/* --- Parsing -------------------------------------------------------------- */

PTEST(parses_frame_and_wait)
{
    const struct replay_command *c = only_command("frame 120");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_FRAME);
    PT_ASSERT_EQ_INT(c->number, 120);
    c = only_command("wait 0");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_WAIT);
    PT_ASSERT_EQ_INT(c->number, 0);
}

PTEST(parses_press_with_every_key_in_any_case)
{
    const struct replay_command *c = only_command("press A,b,Start,SELECT,up,DOWN,Left,RIGHT,L,R");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_PRESS);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEYS_MASK);
}

PTEST(parses_a_single_key_and_spaces_around_commas)
{
    const struct replay_command *c = only_command("  press  A , START  ");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_A | INPUT_KEY_START);
    c = only_command("release L");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_RELEASE);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_L);
}

PTEST(parses_hold_and_hold_for)
{
    const struct replay_command *c = only_command("hold UP");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_HOLD);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_UP);
    c = only_command("hold A,B for 30");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_HOLD_FOR);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_A | INPUT_KEY_B);
    PT_ASSERT_EQ_INT(c->number, 30);
}

PTEST(parses_wait_until_and_expect)
{
    const struct replay_command *c = only_command("wait-until callback2=CB2_Overworld max 600");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_WAIT_UNTIL);
    PT_ASSERT_EQ_STR(c->fact, "callback2");
    PT_ASSERT_EQ_STR(c->value, "CB2_Overworld");
    PT_ASSERT_EQ_INT(c->number, 600);
    c = only_command("expect player_name=ASH KETCHUM");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_EXPECT);
    PT_ASSERT_EQ_STR(c->fact, "player_name");
    PT_ASSERT_EQ_STR(c->value, "ASH KETCHUM");
}

PTEST(a_value_may_contain_max_as_a_word)
{
    const struct replay_command *c = only_command("wait-until player_name=A max B max 5");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_STR(c->value, "A max B");
    PT_ASSERT_EQ_INT(c->number, 5);
}

PTEST(parses_screenshot_dump_and_restart)
{
    const struct replay_command *c = only_command("screenshot 01_title-a");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_SCREENSHOT);
    PT_ASSERT_EQ_STR(c->name, "01_title-a");
    c = only_command("dump after_save");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_DUMP);
    c = only_command("restart");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_RESTART);
}

PTEST(comments_blank_lines_and_line_numbers)
{
    struct replay_parse_error error;
    PT_ASSERT_EQ_INT(parse("# header\n\n  \nwait 1 # trailing\r\n\tpress A\nrestart", &error), 0);
    PT_ASSERT_EQ_INT(script.count, 3);
    PT_ASSERT_EQ_INT(script.commands[0].line, 4);
    PT_ASSERT_EQ_INT(script.commands[1].line, 5);
    PT_ASSERT_EQ_INT(script.commands[2].line, 6);
}

PTEST(an_empty_script_has_no_commands)
{
    struct replay_parse_error error;
    PT_ASSERT_EQ_INT(parse("", &error), 0);
    PT_ASSERT_EQ_INT(script.count, 0);
    PT_ASSERT_EQ_INT(parse("# nothing\n\n", &error), 0);
    PT_ASSERT_EQ_INT(script.count, 0);
}

PTEST(rejects_unknown_command)
{
    assert_parse_error("wait 1\nfrobnicate 3", 2, "unknown command 'frobnicate'");
}

PTEST(rejects_bad_frame_and_wait_counts)
{
    assert_parse_error("frame", 1, "whole number");
    assert_parse_error("frame 0", 1, "whole number");
    assert_parse_error("frame -3", 1, "whole number");
    assert_parse_error("wait x", 1, "whole number");
    assert_parse_error("wait 3x", 1, "whole number");
    assert_parse_error("wait 1 2", 1, "whole number");
    assert_parse_error("wait 100000001", 1, "whole number");
    assert_parse_error("wait 99999999999999999999999", 1, "whole number");
}

PTEST(rejects_bad_keys)
{
    assert_parse_error("press", 1, "unknown key ''");
    assert_parse_error("press X", 1, "unknown key 'X'");
    assert_parse_error("press A,,B", 1, "unknown key ''");
    assert_parse_error("press A,", 1, "unknown key ''");
    assert_parse_error("hold A,NOPE", 1, "unknown key 'NOPE'");
    assert_parse_error("release", 1, "unknown key ''");
    assert_parse_error("press A B", 1, "unknown key 'A B'");
}

PTEST(rejects_bad_hold_for)
{
    assert_parse_error("hold A for", 1, "unknown key");
    assert_parse_error("hold A for 0", 1, "at least 1");
    assert_parse_error("hold A for x", 1, "at least 1");
    assert_parse_error("hold for 5", 1, "unknown key");
}

PTEST(rejects_bad_wait_until)
{
    assert_parse_error("wait-until callback2=X", 1, "needs 'max N'");
    assert_parse_error("wait-until callback2=X max", 1, "needs 'max N'");
    assert_parse_error("wait-until callback2=X max q", 1, "'max' needs");
    assert_parse_error("wait-until callback2 max 5", 1, "expected FACT=VALUE");
    assert_parse_error("wait-until Bad=X max 5", 1, "bad fact name");
    assert_parse_error("wait-until a= max 5", 1, "empty or too long");
}

PTEST(rejects_bad_expect)
{
    assert_parse_error("expect", 1, "expected FACT=VALUE");
    assert_parse_error("expect map", 1, "expected FACT=VALUE");
    assert_parse_error("expect =5", 1, "bad fact name");
    assert_parse_error("expect map=", 1, "empty or too long");
    assert_parse_error("expect has-dash=1", 1, "bad fact name");
}

PTEST(rejects_bad_names_and_restart_arguments)
{
    assert_parse_error("screenshot", 1, "bad name");
    assert_parse_error("screenshot a/b", 1, "bad name");
    assert_parse_error("dump ../x", 1, "bad name");
    assert_parse_error("dump a b", 1, "bad name");
    assert_parse_error("restart now", 1, "no arguments");
}

PTEST(rejects_over_long_things)
{
    char text[REPLAY_MAX_LINE_LENGTH + 64];
    struct replay_parse_error error;
    memset(text, 'x', sizeof text);
    memcpy(text, "expect a=", 9);
    text[REPLAY_MAX_LINE_LENGTH + 1] = '\0';
    PT_ASSERT_EQ_INT(parse(text, &error), -1);
    PT_ASSERT(strstr(error.message, "longer than the limit") != NULL);

    char value[REPLAY_MAX_VALUE_LENGTH + 32];
    memset(value, 'v', sizeof value);
    value[REPLAY_MAX_VALUE_LENGTH + 1] = '\0';
    char line[sizeof value + 16];
    snprintf(line, sizeof line, "expect a=%s", value);
    PT_ASSERT_EQ_INT(parse(line, &error), -1);
    PT_ASSERT(strstr(error.message, "empty or too long") != NULL);

    char name[REPLAY_MAX_NAME_LENGTH + 8];
    memset(name, 'n', sizeof name);
    name[REPLAY_MAX_NAME_LENGTH + 1] = '\0';
    snprintf(line, sizeof line, "dump %s", name);
    PT_ASSERT_EQ_INT(parse(line, &error), -1);
    PT_ASSERT(strstr(error.message, "bad name") != NULL);
}

PTEST(accepts_the_longest_value_and_name)
{
    char line[256];
    char value[REPLAY_MAX_VALUE_LENGTH + 1];
    struct replay_parse_error error;
    memset(value, 'v', sizeof value);
    value[REPLAY_MAX_VALUE_LENGTH] = '\0';
    snprintf(line, sizeof line, "expect a=%s", value);
    PT_ASSERT_EQ_INT(parse(line, &error), 0);
    PT_ASSERT_EQ_STR(script.commands[0].value, value);
}

PTEST(rejects_more_commands_than_the_limit)
{
    size_t size = (REPLAY_MAX_COMMANDS + 1) * 8 + 1;
    char *text = malloc(size);
    PT_ASSERT(text != NULL);
    text[0] = '\0';
    for (size_t i = 0; i <= REPLAY_MAX_COMMANDS; i++)
        strcat(text, "wait 1\n");
    struct replay_parse_error error;
    int result = parse(text, &error);
    free(text);
    PT_ASSERT_EQ_INT(result, -1);
    PT_ASSERT_EQ_INT(error.line, REPLAY_MAX_COMMANDS + 1);
    PT_ASSERT(strstr(error.message, "more commands") != NULL);
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
    struct fake *fake = context;
    for (size_t i = 0; i < FAKE_FACTS; i++) {
        if (strcmp(fake->names[i], name) == 0) {
            snprintf(out, size, "%s", fake->values[i]);
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
    snprintf(fake->last_screenshot, sizeof fake->last_screenshot, "%s", path);
    return fake->screenshot_result;
}

static void fake_set_keys(void *context, uint16_t keys)
{
    struct fake *fake = context;
    fake->keys = keys;
}

static void fake_init(struct fake *fake)
{
    *fake = (struct fake){ 0 };
    strcpy(fake->names[0], "alpha");
    strcpy(fake->names[1], "beta");
    strcpy(fake->names[2], "gamma");
    strcpy(fake->values[0], "one");
    strcpy(fake->values[1], "two");
    strcpy(fake->values[2], "three");
}

static struct replay_host host_for(struct fake *fake)
{
    return (struct replay_host){ .context = fake,
                                 .get_fact = fake_get_fact,
                                 .fact_name = fake_fact_name,
                                 .screenshot = fake_screenshot,
                                 .set_keys = fake_set_keys };
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
    PT_ASSERT_EQ_INT(fake.steps, 5);
    PT_ASSERT_EQ_INT(replay_restart_line(&replay), 3);
}

PTEST(wait_and_frame_count_steps)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait 3\nframe 10\nwait 0\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    /* wait 3 runs at step 1 and ends at step 4; frame 10 ends at step 10, where the rest runs */
    PT_ASSERT_EQ_INT(fake.steps, 10);
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
    PT_ASSERT_EQ_INT(fake.steps, 1);
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
    PT_ASSERT_EQ_INT(fake.steps, 5);
}

PTEST(wait_until_that_already_holds_does_not_wait)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until alpha=one max 5\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_RESTART);
    PT_ASSERT_EQ_INT(fake.steps, 1);
}

PTEST(wait_until_times_out_after_exactly_max_frames)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait 2\nwait-until alpha=never max 7\nrestart", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    /* the wait-until starts at step 3; it may still succeed at step 3 + 7 and fails there */
    PT_ASSERT_EQ_INT(fake.steps, 10);
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
    PT_ASSERT_EQ_INT(fake.steps, 4);
}

PTEST(wait_until_max_zero_requires_the_fact_now)
{
    struct replay replay;
    struct fake fake;
    PT_ASSERT_EQ_INT(start(&replay, &fake, "wait-until alpha=go max 0", 0), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_FAILED);
    PT_ASSERT_EQ_INT(fake.steps, 1);
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
    PT_ASSERT_EQ_INT(start(&replay, &fake, "expect alpha=bad\n# c\nrestart\nexpect alpha=one\nwait 2\nexpect beta=two", 3), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_DONE);
    PT_ASSERT_EQ_INT(fake.steps, 3);
    PT_ASSERT_EQ_INT(start(&replay, &fake, "restart\nrestart\nwait 1", 99), 0);
    PT_ASSERT_EQ_INT(run(&replay, &fake, 100), REPLAY_DONE);
    PT_ASSERT_EQ_INT(fake.steps, 1);
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
    snprintf(out_dir, sizeof out_dir, "/tmp/test_replay_XXXXXX");
    PT_ASSERT(mkdtemp(out_dir) != NULL);
}

static void remove_out_dir(void)
{
    char path[REPLAY_PATH_SIZE];
    snprintf(path, sizeof path, "%s/d1.txt", out_dir);
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
    snprintf(path, sizeof path, "%s/d1.txt", out_dir);
    FILE *file = fopen(path, "r");
    size_t length = file != NULL ? fread(content, 1, sizeof content - 1, file) : 0;
    if (file != NULL)
        fclose(file);
    content[length] = '\0';
    remove_out_dir();
    PT_ASSERT_EQ_INT(status, REPLAY_RESTART);
    PT_ASSERT_EQ_STR(content, "alpha=one\nbeta=two\ngamma=three\n");
    PT_ASSERT_EQ_INT(fake.screenshots, 1);
    snprintf(path, sizeof path, "%s/s1.ppm", out_dir);
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
    struct replay_script empty = { .count = 0 };
    struct replay_host host = { 0 };
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
