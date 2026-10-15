/*
 * test_version.c: the build's version string (version.c) is a word that
 * prints on one line: what Makefile.native lets through from git describe
 * (letters, digits and ._+-), or "unknown".
 */
#include "ptest.h"
#include "version.h"

#include <stdbool.h>
#include <string.h>

static bool is_version_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("._+-", c) != NULL;
}

PTEST(the_version_is_a_word_of_the_characters_git_describe_gives)
{
    const char *const version = version_string();
    PT_ASSERT(version != NULL);
    PT_ASSERT(version[0] != '\0');
    for (const char *c = version; *c != '\0'; c++)
        PT_ASSERT(is_version_char(*c));
    PT_ASSERT_EQ_STR(VERSION_PROGRAM_NAME, "pokeemerald-native");
}

PTEST_MAIN()
