/*
 * test_replay_parse.c: the replay script parser (replay_parse.c). Every command
 * is parsed in its good form and each malformed form.
 */
#include "ptest.h"
#include "replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"

static struct replay_script script;

static int parse(const char *text, struct replay_parse_error *error)
{
    return replay_parse(text, &script, error);
}

static void assert_parse_error(const char *text, uint32_t line, const char *needle)
{
    struct replay_parse_error error = {0};
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

PTEST(parses_press_times_and_defaults_to_one_press)
{
    const struct replay_command *c = only_command("press UP,A times 12");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_PRESS);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_UP | INPUT_KEY_A);
    PT_ASSERT_EQ_INT(c->number, 12);
    c = only_command("press   A   times\t1   # comment");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, 1);
    c = only_command("press B");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, 1);
    c = only_command("press A times    3");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, 3);
    c = only_command("press A times 10000");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, 10000);
}

PTEST(parses_a_fact_name_with_an_argument)
{
    const struct replay_command *c = only_command("expect flag:2078=1");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_STR(c->fact, "flag:2078");
    PT_ASSERT_EQ_STR(c->value, "1");
    c = only_command("wait-until bag_item_count:293=1 max 5");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_STR(c->fact, "bag_item_count:293");
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

PTEST(parses_mash)
{
    const struct replay_command *c = only_command("mash A,B until task_first=Task_X max 900");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_MASH);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_A | INPUT_KEY_B);
    PT_ASSERT_EQ_STR(c->fact, "task_first");
    PT_ASSERT_EQ_STR(c->value, "Task_X");
    PT_ASSERT_EQ_INT(c->number, 900);
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
    PT_ASSERT(script.count == 3);
    PT_ASSERT_EQ_INT(script.commands[0].line, 4);
    PT_ASSERT_EQ_INT(script.commands[1].line, 5);
    PT_ASSERT_EQ_INT(script.commands[2].line, 6);
}

PTEST(an_empty_script_has_no_commands)
{
    struct replay_parse_error error;
    PT_ASSERT_EQ_INT(parse("", &error), 0);
    PT_ASSERT(script.count == 0);
    PT_ASSERT_EQ_INT(parse("# nothing\n\n", &error), 0);
    PT_ASSERT(script.count == 0);
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

PTEST(rejects_bad_press_times)
{
    assert_parse_error("press A times", 1, "unknown key");
    assert_parse_error("press A times 0", 1, "at least 1");
    assert_parse_error("press A times x", 1, "at least 1");
    assert_parse_error("press A times -3", 1, "at least 1");
    assert_parse_error("press A times 10001", 1, "at most 10000");
    assert_parse_error("press times 3", 1, "unknown key");
    assert_parse_error("press X times 3", 1, "unknown key 'X'");
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

PTEST(rejects_bad_mash)
{
    assert_parse_error("mash A", 1, "mash needs 'until");
    assert_parse_error("mash A until", 1, "mash needs 'until");
    assert_parse_error("mash until a=b max 3", 1, "mash needs 'until");
    assert_parse_error("mash X until a=b max 3", 1, "unknown key 'X'");
    assert_parse_error("mash A until a=b", 1, "mash needs 'max N'");
    assert_parse_error("mash A until a=b max x", 1, "'max' needs");
    assert_parse_error("mash A until ab max 3", 1, "expected FACT=VALUE");
}

PTEST(rejects_bad_expect)
{
    assert_parse_error("expect", 1, "expected FACT=VALUE");
    assert_parse_error("expect map", 1, "expected FACT=VALUE");
    assert_parse_error("expect =5", 1, "bad fact name");
    assert_parse_error("expect map=", 1, "empty or too long");
    assert_parse_error("expect has-dash=1", 1, "bad fact name");
    assert_parse_error("expect Flag:1=1", 1, "bad fact name");
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
    (void)snprintf(line, sizeof line, "expect a=%s", value);
    PT_ASSERT_EQ_INT(parse(line, &error), -1);
    PT_ASSERT(strstr(error.message, "empty or too long") != NULL);

    char name[REPLAY_MAX_NAME_LENGTH + 8];
    memset(name, 'n', sizeof name);
    name[REPLAY_MAX_NAME_LENGTH + 1] = '\0';
    (void)snprintf(line, sizeof line, "dump %s", name);
    PT_ASSERT_EQ_INT(parse(line, &error), -1);
    PT_ASSERT(strstr(error.message, "bad name") != NULL);
}

PTEST(accepts_the_longest_value_and_name)
{
    char line[REPLAY_MAX_LINE_LENGTH + 1];
    char value[REPLAY_MAX_VALUE_LENGTH + 1];
    struct replay_parse_error error;
    memset(value, 'v', sizeof value);
    value[REPLAY_MAX_VALUE_LENGTH] = '\0';
    (void)snprintf(line, sizeof line, "expect a=%s", value);
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
        memcpy(text + i * 7, "wait 1\n", 8); /* with its terminator, overwritten by the next */
    struct replay_parse_error error;
    int result = parse(text, &error);
    free(text);
    PT_ASSERT_EQ_INT(result, -1);
    PT_ASSERT_EQ_INT(error.line, REPLAY_MAX_COMMANDS + 1);
    PT_ASSERT(strstr(error.message, "more commands") != NULL);
}

PTEST(tab_separators_are_blanks)
{
    const struct replay_command *c = only_command("press\tA,\tSTART");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_A | INPUT_KEY_START);
    c = only_command("wait-until\tmap=1.0\tmax\t5");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_STR(c->value, "1.0");
    PT_ASSERT_EQ_INT(c->number, 5);
}

PTEST(a_nul_byte_in_a_buffer_is_an_error_at_its_line)
{
    const char bytes[] = "wait 1\nwait 2\0wait 3\n";
    struct replay_parse_error error;
    PT_ASSERT_EQ_INT(replay_parse_buffer(bytes, sizeof bytes - 1, &script, &error), -1);
    PT_ASSERT_EQ_INT(error.line, 2);
    PT_ASSERT(strstr(error.message, "NUL") != NULL);
    const char fine[] = "wait 1\nwait 2\n";
    PT_ASSERT_EQ_INT(replay_parse_buffer(fine, sizeof fine - 1, &script, &error), 0);
    PT_ASSERT(script.count == 2);
}

PTEST(frame_after_restart_is_rejected)
{
    assert_parse_error("frame 5\nrestart\nwait 1\nframe 9", 4, "after 'restart'");
    struct replay_parse_error error;
    PT_ASSERT_EQ_INT(parse("wait 1\nframe 5\nrestart\nwait 1", &error), 0);
}

PTEST_MAIN()

/* The "\r" of a CRLF file is a blank at the end of every command's last word. */
PTEST(crlf_line_endings_end_every_kind_of_command)
{
    const struct replay_command *c = only_command("press A\r\n");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_A);
    c = only_command("wait-until a=b max 5\r\n");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_STR(c->value, "b");
    PT_ASSERT_EQ_INT(c->number, 5);
    c = only_command("dump x\r\n");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_STR(c->name, "x");
    c = only_command("restart\r\n");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_RESTART);
}

PTEST(the_largest_count_is_accepted_for_every_command_that_takes_one)
{
    const struct replay_command *c = only_command("wait 100000000");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, REPLAY_MAX_FRAMES);
    c = only_command("hold A for 100000000");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, REPLAY_MAX_FRAMES);
    c = only_command("press A times 10000");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, REPLAY_MAX_PRESS_TIMES);
    c = only_command("wait-until a=b max 100000000");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, REPLAY_MAX_FRAMES);
    c = only_command("mash A until a=b max 100000000");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->number, REPLAY_MAX_FRAMES);
}

PTEST(a_count_past_the_largest_or_past_32_bits_is_rejected_for_for_times_and_max)
{
    assert_parse_error("hold A for 100000001", 1, "at least 1");
    assert_parse_error("hold A for 99999999999999999999999", 1, "at least 1");
    assert_parse_error("press A times 10001", 1, "at most");
    assert_parse_error("press A times 99999999999999999999999", 1, "at most");
    assert_parse_error("wait-until a=b max 100000001", 1, "'max' needs");
    assert_parse_error("wait-until a=b max 99999999999999999999999", 1, "'max' needs");
    assert_parse_error("mash A until a=b max 100000001", 1, "'max' needs");
}

/* A key named twice is one key: the elements are ORed together. */
PTEST(a_key_named_twice_is_one_key)
{
    const struct replay_command *c = only_command("press A,A");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_A);
    c = only_command("hold b,B,a");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_A | INPUT_KEY_B);
}

PTEST(release_names_its_keys_whether_or_not_they_were_held)
{
    const struct replay_command *c = only_command("release L,R");
    PT_ASSERT(c != NULL);
    PT_ASSERT_EQ_INT(c->kind, REPLAY_CMD_RELEASE);
    PT_ASSERT_EQ_U32(c->keys, INPUT_KEY_L | INPUT_KEY_R);
}
