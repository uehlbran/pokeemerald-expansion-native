// deps: replay_include.c
/*
 * test_replay_include.c: `include FILE` expansion (replay_include.c) on real
 * files in a temporary directory.
 */
#include "ptest.h"
#include "replay_include.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[64];

static void make_directory(void)
{
    snprintf(directory, sizeof directory, "/tmp/replay_include_test_XXXXXX");
    if (mkdtemp(directory) == NULL) {
        perror("mkdtemp");
        exit(2);
    }
}

static void write_file(const char *name, const char *text)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", directory, name);
    FILE *file = fopen(path, "wb");
    if (file == NULL || fputs(text, file) < 0 || fclose(file) != 0) {
        perror(path);
        exit(2);
    }
}

static void path_of(const char *name, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", directory, name);
}

static void remove_all(void)
{
    char command[256];
    snprintf(command, sizeof command, "rm -rf '%s'", directory);
    if (system(command) != 0)
        perror("rm");
}

/* Expands `name`; text and message are the caller's to free (text) or read. */
static int expand(const char *name, char **text, bool *expanded, char *message, size_t message_size)
{
    char path[256];
    size_t length;
    path_of(name, path, sizeof path);
    int result = replay_include_expand(path, text, &length, expanded, message, message_size);
    if (result == 0 && strlen(*text) != length)
        return -2; /* the reported length is wrong */
    return result;
}

PTEST(a_script_without_includes_is_returned_as_it_is)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded = true;
    make_directory();
    write_file("plain.replay", "wait 1\npress A # a comment\n");
    PT_ASSERT_EQ_INT(expand("plain.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "wait 1\npress A # a comment\n");
    PT_ASSERT(!expanded);
    free(text);
    remove_all();
}

PTEST(an_include_is_replaced_by_the_lines_of_the_file_and_keeps_the_line_count_flat)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded = false;
    make_directory();
    write_file("part.replay", "press A\nwait 2");
    write_file("main.replay", "wait 1\n  include   part.replay   # the start\nrestart\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "wait 1\npress A\nwait 2\nrestart\n"); /* a missing final newline is supplied */
    PT_ASSERT(expanded);
    free(text);
    remove_all();
}

PTEST(includes_nest_and_resolve_next_to_the_including_file)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded = false;
    char path[256];
    make_directory();
    snprintf(path, sizeof path, "%s/sub", directory);
    PT_ASSERT_EQ_INT(mkdir(path, 0777), 0);
    write_file("sub/inner.replay", "wait 3\n");
    write_file("sub/middle.replay", "include inner.replay\nwait 2\n");
    write_file("main.replay", "include sub/middle.replay\nwait 1\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "wait 3\nwait 2\nwait 1\n");
    free(text);
    remove_all();
}

PTEST(the_same_file_may_be_included_twice)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded = false;
    make_directory();
    write_file("part.replay", "wait 1\n");
    write_file("main.replay", "include part.replay\ninclude part.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "wait 1\nwait 1\n");
    free(text);
    remove_all();
}

PTEST(words_that_merely_start_with_include_and_comments_are_not_includes)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded = true;
    make_directory();
    write_file("main.replay", "# include nothing.replay\nincluded x\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "# include nothing.replay\nincluded x\n");
    PT_ASSERT(!expanded);
    free(text);
    remove_all();
}

PTEST(an_empty_file_expands_to_an_empty_string)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded = false;
    make_directory();
    write_file("empty.replay", "");
    write_file("main.replay", "include empty.replay\n");
    PT_ASSERT_EQ_INT(expand("empty.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "");
    free(text);
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "");
    free(text);
    remove_all();
}

PTEST(a_missing_file_is_named_with_the_error)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text = (char *)1;
    bool expanded = true;
    make_directory();
    write_file("main.replay", "wait 1\ninclude nope.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(text == NULL);
    PT_ASSERT(!expanded);
    PT_ASSERT(strstr(message, "nope.replay") != NULL);
    PT_ASSERT(strstr(message, "cannot open") != NULL);
    remove_all();
}

PTEST(a_missing_top_file_is_an_error)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    PT_ASSERT_EQ_INT(expand("absent.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "absent.replay") != NULL);
    remove_all();
}

PTEST(an_include_without_a_name_is_an_error_with_its_line)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    write_file("main.replay", "wait 1\ninclude # nothing\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "main.replay:2:") != NULL);
    PT_ASSERT(strstr(message, "needs a file name") != NULL);
    write_file("main.replay", "include\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "main.replay:1:") != NULL);
    remove_all();
}

PTEST(a_file_that_includes_itself_is_refused)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text = (char *)1;
    bool expanded;
    make_directory();
    write_file("loop.replay", "wait 1\ninclude loop.replay\n");
    PT_ASSERT_EQ_INT(expand("loop.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(text == NULL);
    PT_ASSERT(strstr(message, "too deep") != NULL);
    remove_all();
}

PTEST(a_chain_of_exactly_the_maximum_depth_is_accepted_and_one_more_is_not)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    /* f0 includes f1 ... f4 (depth 4, the limit); f5 would be depth 5 */
    write_file("f4.replay", "wait 4\n");
    write_file("f3.replay", "include f4.replay\n");
    write_file("f2.replay", "include f3.replay\n");
    write_file("f1.replay", "include f2.replay\n");
    write_file("f0.replay", "include f1.replay\n");
    PT_ASSERT_EQ_INT(expand("f0.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "wait 4\n");
    free(text);
    write_file("f4.replay", "include f5.replay\n");
    write_file("f5.replay", "wait 5\n");
    PT_ASSERT_EQ_INT(expand("f0.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "too deep") != NULL);
    remove_all();
}

PTEST(an_expansion_larger_than_the_limit_is_refused)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    char *big = malloc(REPLAY_INCLUDE_MAX_BYTES / 2u + 1u);
    PT_ASSERT(big != NULL);
    memset(big, '#', REPLAY_INCLUDE_MAX_BYTES / 2u);
    big[REPLAY_INCLUDE_MAX_BYTES / 2u] = '\0';
    make_directory();
    write_file("big.replay", big);
    free(big);
    write_file("main.replay", "include big.replay\ninclude big.replay\ninclude big.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(text == NULL);
    PT_ASSERT(strstr(message, "larger than the limit") != NULL);
    remove_all();
}

PTEST(a_tiny_message_buffer_is_cut_and_terminated)
{
    char message[8];
    char *text;
    bool expanded;
    make_directory();
    write_file("main.replay", "include nope.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strlen(message) == sizeof message - 1u);
    remove_all();
}

PTEST_MAIN()
