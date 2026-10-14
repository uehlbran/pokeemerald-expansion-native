// deps: replay_include.c
/*
 * test_replay_include_posix.c: the part of replay_include.c's tests that needs
 * POSIX path handling. The rest is in ../test_replay_include.c.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ptest.h"
#include "replay_include.h"
#include "test_support.h"

/* POSIX only: repeated slashes pad a path to the limit and still name the
 * file, which Windows' file calls refuse past MAX_PATH (260 characters). */
PTEST(a_script_path_of_the_maximum_length_is_described_whole_in_the_documented_buffer)
{
    char directory[128];
    char script[256];
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    char where[REPLAY_SOURCE_WHERE_SIZE];
    char path[REPLAY_INCLUDE_MAX_NAME + 1u];
    struct replay_source_map map = {0};
    char *text = NULL;
    size_t text_length = 0;
    bool expanded;
    PT_ASSERT(tsup_make_temp_directory(directory, sizeof directory, "replay_include_posix"));
    (void)snprintf(script, sizeof script, "%s/main.replay", directory);
    FILE *const file = fopen(script, "wb");
    const bool written = file != NULL && fputs("wait 1\n", file) >= 0;
    const bool closed = file != NULL && fclose(file) == 0;
    /* Repeated slashes pad the directory's path to the longest name a script may have. */
    const char *const name = "main.replay";
    const size_t directory_length = strlen(directory);
    (void)snprintf(path, sizeof path, "%s", directory);
    memset(path + directory_length, '/', REPLAY_INCLUDE_MAX_NAME - directory_length);
    memcpy(path + REPLAY_INCLUDE_MAX_NAME - strlen(name), name, strlen(name) + 1u);
    const int expansion = replay_include_expand(path, &text, &text_length, &expanded, &map, message, sizeof message);
    const int result = expansion == 0 && strlen(text) != text_length ? -2 : expansion; /* -2: the length is wrong */
    replay_source_map_describe(&map, 1, where, sizeof where);
    const size_t length = strlen(where);
    free(text);
    replay_source_map_free(&map);
    (void)tsup_remove_tree(directory);
    PT_ASSERT(written && closed);
    PT_ASSERT_EQ_INT((int)strlen(path), (int)REPLAY_INCLUDE_MAX_NAME);
    PT_ASSERT_EQ_INT(result, 0);
    PT_ASSERT_EQ_INT((int)length, (int)REPLAY_INCLUDE_MAX_NAME + 2);
    PT_ASSERT_EQ_STR(where + length - 2u, ":1");
}

PTEST_MAIN()
