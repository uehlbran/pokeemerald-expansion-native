// deps: replay_include.c
/*
 * test_replay_include.c: `include FILE` expansion (replay_include.c) on real
 * files in a temporary directory.
 */
#include "ptest.h"
#include "replay_include.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char directory[128];

static void make_directory(void)
{
    if (!tsup_make_temp_directory(directory, sizeof directory, "replay_include_test")) {
        perror("tsup_make_temp_directory");
        exit(2);
    }
}

static void write_file(const char *name, const char *text)
{
    char path[256];
    (void)snprintf(path, sizeof path, "%s/%s", directory, name);
    FILE *file = fopen(path, "wb");
    if (file == NULL || fputs(text, file) < 0 || fclose(file) != 0) {
        perror(path);
        exit(2);
    }
}

static void path_of(const char *name, char *out, size_t size)
{
    (void)snprintf(out, size, "%s/%s", directory, name);
}

static void remove_all(void)
{
    if (!tsup_remove_tree(directory))
        perror("tsup_remove_tree");
}

static struct replay_source_map map;

/* Expands `name` in the test directory; text is the caller's to free; `map` is filled (static,
 * released by the next call). */
static int expand_path(const char *path, char **text, bool *expanded, char *message, size_t message_size)
{
    size_t length;
    replay_source_map_free(&map);
    int result = replay_include_expand(path, text, &length, expanded, &map, message, message_size);
    if (result == 0 && strlen(*text) != length)
        return -2; /* the reported length is wrong */
    return result;
}

static int expand(const char *name, char **text, bool *expanded, char *message, size_t message_size)
{
    char path[256];
    path_of(name, path, sizeof path);
    return expand_path(path, text, expanded, message, message_size);
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
    (void)snprintf(path, sizeof path, "%s/sub", directory);
    PT_ASSERT(tsup_make_directory(path));
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

static void describe(uint32_t flat_line, char *out, size_t size)
{
    replay_source_map_describe(&map, flat_line, out, size);
}

PTEST(lines_map_back_to_the_file_and_line_they_came_from)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char where[256];
    char *text;
    bool expanded = false;
    make_directory();
    write_file("inc.replay", "press A\nwait 2\n");
    write_file("main.replay", "wait 1\ninclude inc.replay\nrestart\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "wait 1\npress A\nwait 2\nrestart\n");
    describe(1, where, sizeof where);
    PT_ASSERT(strstr(where, "main.replay:1") != NULL);
    describe(2, where, sizeof where);
    PT_ASSERT(strstr(where, "inc.replay:1") != NULL);
    describe(3, where, sizeof where);
    PT_ASSERT(strstr(where, "inc.replay:2") != NULL);
    describe(4, where, sizeof where);
    PT_ASSERT(strstr(where, "main.replay:3") != NULL); /* the include line itself does not count */
    describe(0, where, sizeof where);
    PT_ASSERT_EQ_STR(where, "line 0");
    describe(5, where, sizeof where);
    PT_ASSERT_EQ_STR(where, "line 5");
    free(text);
    replay_source_map_free(&map);
    replay_source_map_free(&map); /* twice is safe */
    remove_all();
}

PTEST(a_map_description_is_cut_to_the_buffer)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char where[6];
    char *text;
    bool expanded;
    make_directory();
    write_file("main.replay", "wait 1\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    describe(1, where, sizeof where);
    PT_ASSERT(strlen(where) == sizeof where - 1u);
    describe(1, where, 0); /* a zero-size buffer is left alone */
    free(text);
    replay_source_map_free(&map);
    remove_all();
}

PTEST(a_failure_in_an_included_file_names_that_file_and_where_it_was_included_from)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    write_file("inner.replay", "wait 1\ninclude # no name\n");
    write_file("main.replay", "wait 1\nwait 2\ninclude inner.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "inner.replay:2: include needs a file name") != NULL);
    PT_ASSERT(strstr(message, "(included at ") != NULL);
    PT_ASSERT(strstr(message, "main.replay:3)") != NULL);
    remove_all();
}

PTEST(a_missing_include_carries_the_including_file_and_line)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    write_file("main.replay", "wait 1\nwait 2\ninclude nope.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "nope.replay: cannot open") != NULL);
    PT_ASSERT(strstr(message, "main.replay:3)") != NULL);
    remove_all();
}

PTEST(a_file_that_includes_itself_names_the_including_line)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    write_file("loop.replay", "wait 1\ninclude loop.replay\n");
    PT_ASSERT_EQ_INT(expand("loop.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "too deep") != NULL);
    PT_ASSERT(strstr(message, "loop.replay:2)") != NULL);
    remove_all();
}

PTEST(crlf_line_endings_work_with_and_without_a_name)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded = false;
    make_directory();
    write_file("part.replay", "press A\r\n");
    write_file("main.replay", "include part.replay\r\nwait 1\r\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "press A\r\nwait 1\r\n"); /* the CR stays; the parser treats it as a blank */
    PT_ASSERT(expanded);
    free(text);
    write_file("main.replay", "include\r\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "needs a file name") != NULL);
    write_file("main.replay", "include  \t \r\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "needs a file name") != NULL);
    remove_all();
}

static void fill(char *buffer, size_t length, char c)
{
    memset(buffer, c, length);
    buffer[length] = '\0';
}

PTEST(an_absolute_path_is_used_as_it_is)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char text_line[300];
    char *text;
    char absolute[256];
    bool expanded = false;
    make_directory();
    write_file("elsewhere.replay", "wait 9\n");
    path_of("elsewhere.replay", absolute, sizeof absolute);
    (void)snprintf(text_line, sizeof text_line, "include %s\n", absolute);
    write_file("main.replay", text_line);
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT_EQ_STR(text, "wait 9\n");
    free(text);
    remove_all();
}

PTEST(include_names_of_1023_and_1024_characters)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    char line[REPLAY_INCLUDE_MAX_NAME + 64u];
    char name[REPLAY_INCLUDE_MAX_NAME + 8u];
    make_directory();
    /* 1023 characters, absolute: accepted as a name (opening it fails, not the length; the message is
     * cut to its buffer, which the long path fills) */
    name[0] = '/';
    fill(name + 1, REPLAY_INCLUDE_MAX_NAME - 1u, 'x');
    (void)snprintf(line, sizeof line, "include %s\n", name);
    write_file("main.replay", line);
    int result_1023 = expand("main.replay", &text, &expanded, message, sizeof message);
    bool named_1023 = strstr(message, "xxxxxxxx") != NULL && strstr(message, "longer than the limit") == NULL
                      && strstr(message, "path is too long") == NULL;
    /* 1024 characters: refused for its length, with the line */
    fill(name + 1, REPLAY_INCLUDE_MAX_NAME, 'x');
    (void)snprintf(line, sizeof line, "include %s\n", name);
    write_file("main.replay", line);
    int result_1024 = expand("main.replay", &text, &expanded, message, sizeof message);
    bool refused_1024 = strstr(message, "main.replay:1:") != NULL
                        && strstr(message, "include name is longer than the limit") != NULL;
    /* a name of 1023 characters next to a file in a directory: the path does not fit, its own message */
    fill(name, REPLAY_INCLUDE_MAX_NAME, 'y');
    (void)snprintf(line, sizeof line, "include %s\n", name);
    write_file("main.replay", line);
    int result_path = expand("main.replay", &text, &expanded, message, sizeof message);
    bool path_message = strstr(message, "include path is too long") != NULL;
    remove_all();
    PT_ASSERT_EQ_INT(result_1023, -1);
    PT_ASSERT(named_1023);
    PT_ASSERT_EQ_INT(result_1024, -1);
    PT_ASSERT(refused_1024);
    PT_ASSERT_EQ_INT(result_path, -1);
    PT_ASSERT(path_message);
}

PTEST(including_a_directory_is_a_read_error)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    char path[256];
    make_directory();
    (void)snprintf(path, sizeof path, "%s/adir", directory);
    PT_ASSERT(tsup_make_directory(path));
    write_file("main.replay", "include adir\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "adir") != NULL);
    PT_ASSERT(strstr(message, "cannot read") != NULL || strstr(message, "cannot open") != NULL);
    remove_all();
}

/* A file of `bytes` bytes: lines of 4095 characters, the last one sized to fit, each with a newline. */
static void write_sized_file(const char *name, size_t bytes)
{
    char *data = malloc(bytes + 1u);
    PT_ASSERT(data != NULL);
    memset(data, '#', bytes);
    for (size_t at = 4095u; at < bytes; at += 4096u)
        data[at] = '\n';
    data[bytes - 1u] = '\n';
    data[bytes] = '\0';
    write_file(name, data);
    free(data);
}

PTEST(a_file_of_exactly_the_maximum_size_is_accepted_and_one_byte_more_is_not)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    write_sized_file("max.replay", REPLAY_INCLUDE_MAX_BYTES);
    PT_ASSERT_EQ_INT(expand("max.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT(strlen(text) == REPLAY_INCLUDE_MAX_BYTES);
    free(text);
    write_sized_file("over.replay", REPLAY_INCLUDE_MAX_BYTES + 1u);
    PT_ASSERT_EQ_INT(expand("over.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(text == NULL);
    PT_ASSERT(strstr(message, "larger than the limit") != NULL);
    remove_all();
}

PTEST(a_total_of_exactly_the_maximum_is_accepted_and_one_byte_more_is_not)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    make_directory();
    write_sized_file("half.replay", REPLAY_INCLUDE_MAX_BYTES / 2u);
    write_file("main.replay", "include half.replay\ninclude half.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), 0);
    PT_ASSERT(strlen(text) == REPLAY_INCLUDE_MAX_BYTES);
    free(text);
    write_file("main.replay", "include half.replay\ninclude half.replay\nw\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "larger than the limit") != NULL);
    remove_all();
}

PTEST(a_file_with_a_nul_byte_is_refused_with_its_offset)
{
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char *text;
    bool expanded;
    char path[256];
    FILE *file;
    make_directory();
    path_of("nul.replay", path, sizeof path);
    file = fopen(path, "wb");
    PT_ASSERT(file != NULL);
    PT_ASSERT(fwrite("wait 1\nwa\0it 2\n", 1, 16, file) == 16);
    PT_ASSERT_EQ_INT(fclose(file), 0);
    PT_ASSERT_EQ_INT(expand("nul.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(text == NULL);
    PT_ASSERT(strstr(message, "nul.replay") != NULL);
    PT_ASSERT(strstr(message, "contains a NUL byte at offset 9") != NULL);
    write_file("main.replay", "include nul.replay\n");
    PT_ASSERT_EQ_INT(expand("main.replay", &text, &expanded, message, sizeof message), -1);
    PT_ASSERT(strstr(message, "NUL byte at offset 9") != NULL);
    PT_ASSERT(strstr(message, "main.replay:1)") != NULL);
    remove_all();
}

PTEST_MAIN()
