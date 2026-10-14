/*
 * host_file_fixture.h: what test_host_file.c and posix/test_host_file_posix.c
 * share: a fresh scratch directory per test under the temporary directory,
 * and the little file reads and writes the tests assert on. Static inline,
 * so that a binary need not use them all.
 */
#ifndef PLATFORM_TEST_HOST_FILE_FIXTURE_H
#define PLATFORM_TEST_HOST_FILE_FIXTURE_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "test_support.h"

#define SCRATCH_PATH_SIZE 512u
#define TEXT_SIZE 64u

struct scratch {
    char dir[SCRATCH_PATH_SIZE / 2u];
    char path[SCRATCH_PATH_SIZE];    /* dir/file: the file replaced */
    char from[SCRATCH_PATH_SIZE];    /* dir/file.tmp: what replaces it */
    char made[SCRATCH_PATH_SIZE];    /* dir/made: a directory to create */
    char missing[SCRATCH_PATH_SIZE]; /* dir/missing/file: under no directory */
};

static inline bool scratch_create(struct scratch *scratch)
{
    if (!tsup_make_temp_directory(scratch->dir, sizeof scratch->dir, "ptest_host_file"))
        return false;
    (void)snprintf(scratch->path, sizeof scratch->path, "%s/file", scratch->dir);
    (void)snprintf(scratch->from, sizeof scratch->from, "%s/file.tmp", scratch->dir);
    (void)snprintf(scratch->made, sizeof scratch->made, "%s/made", scratch->dir);
    (void)snprintf(scratch->missing, sizeof scratch->missing, "%s/missing/file", scratch->dir);
    return true;
}

/* Removes what the tests may have left; true if the directory went. */
static inline bool scratch_remove(const struct scratch *scratch)
{
    (void)tsup_remove_file(scratch->path);
    (void)tsup_remove_file(scratch->from);
    (void)tsup_remove_directory(scratch->made);
    return tsup_remove_directory(scratch->dir);
}

static inline bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
        return false;
    const bool written = fputs(text, file) != EOF;
    return fclose(file) == 0 && written;
}

/* The file's text, or "" if it cannot be read. */
static const char *read_text(const char *path)
{
    static char text[TEXT_SIZE];
    text[0] = '\0';
    FILE *file = fopen(path, "r");
    if (file != NULL) {
        text[fread(text, 1, sizeof text - 1u, file)] = '\0';
        (void)fclose(file);
    }
    return text;
}

static inline bool exists(const char *path)
{
    return tsup_exists(path);
}

#endif /* PLATFORM_TEST_HOST_FILE_FIXTURE_H */
