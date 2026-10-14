/*
 * test_save_lock.c: the save's lock (save_lock.h) within one process, where
 * another open of the lock file stands for another instance (host_file.h:
 * the lock is the open file's): the lock file made beside the save and kept,
 * the one line that refuses a save someone else holds, a lock file that
 * cannot be opened and a save path too long for one, each reported by call,
 * and a release that is safe at any time. Another process holding the lock,
 * the lock across a store, a holder killed and a restart:
 * posix/test_save_lock_posix.c and windows/test_save_lock_windows.c.
 * Scratch directories: save_file_fixture.h.
 */
// deps: save_file.c gba_memmap.c flash.c
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "host_file.h"
#include "save_file_fixture.h"
#include "save_lock.h"

/* Room for a report naming a path of the longest length twice. */
#define MESSAGE_SIZE (3u * SAVE_FILE_PATH_SIZE)

/* The longest save path whose lock file's path fits host_file.h's buffer. */
#define LONGEST_SAVE_PATH (HOST_FILE_PATH_SIZE - sizeof LOCK_SUFFIX)

struct take {
    const char *save_path;
    int status;
};

static void take_the_lock(void *context)
{
    struct take *const take = context;
    take->status = save_lock_take(take->save_path);
}

/* save_lock_take(save_path) with what it printed in text. */
static int take_and_capture(const char *save_path, char *text)
{
    struct take take = {.save_path = save_path, .status = SAVE_LOCK_OK};
    if (!tsup_capture_stderr(take_the_lock, &take, text, MESSAGE_SIZE))
        (void)snprintf(text, MESSAGE_SIZE, "(stderr not captured)");
    return take.status;
}

/* True if another open of the lock file can take its lock now, which it then
 * releases. */
static bool free_to_take(const char *lock_path)
{
    const int descriptor = host_file_open(lock_path, O_RDWR | O_CREAT);
    if (descriptor < 0 || host_file_take_lock(descriptor) != 0) {
        if (descriptor >= 0)
            (void)host_file_release_lock(descriptor); /* closes it; there is no lock to drop */
        return false;
    }
    return host_file_release_lock(descriptor) == 0;
}

PTEST(the_lock_file_is_made_beside_the_save_and_kept_after_the_release)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int taken = save_lock_take(scratch.path);
    const bool made = exists(scratch.lock);
    const intmax_t size = file_size(scratch.lock);
    const bool save_made = exists(scratch.path);
    const bool held = !free_to_take(scratch.lock);
    save_lock_release();
    const bool kept = exists(scratch.lock);
    const bool freed = free_to_take(scratch.lock);
    const int taken_again = save_lock_take(scratch.path); /* the file's presence means nothing */
    save_lock_release();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(taken, SAVE_LOCK_OK);
    PT_ASSERT(made);
    PT_ASSERT_EQ_INT(size, 0);
    PT_ASSERT(!save_made); /* the lock touches the save's directory, never the save */
    PT_ASSERT(held);
    PT_ASSERT(kept);
    PT_ASSERT(freed);
    PT_ASSERT_EQ_INT(taken_again, SAVE_LOCK_OK);
    PT_ASSERT(removed);
}

PTEST(a_save_held_elsewhere_is_refused_with_one_line_naming_it)
{
    static char text[MESSAGE_SIZE];
    static char expected[MESSAGE_SIZE];
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int other = host_file_open(scratch.lock, O_RDWR | O_CREAT);
    const int other_took = host_file_take_lock(other);
    const int refused = take_and_capture(scratch.path, text);
    const int other_released = host_file_release_lock(other);
    const int taken_after = save_lock_take(scratch.path); /* the refusal left nothing held or broken */
    save_lock_release();
    const bool removed = scratch_remove(&scratch);
    (void)snprintf(expected, sizeof expected,
                   "save_lock: another instance of the game holds the save %s (the lock on %s); not starting, so "
                   "that the two cannot write over each other's saves\n",
                   scratch.path, scratch.lock);
    PT_ASSERT_EQ_INT(other_took, 0);
    PT_ASSERT_EQ_INT(refused, SAVE_LOCK_HELD);
    PT_ASSERT_EQ_STR(text, expected);
    PT_ASSERT_EQ_INT(other_released, 0);
    PT_ASSERT_EQ_INT(taken_after, SAVE_LOCK_OK);
    PT_ASSERT(removed);
}

PTEST(a_lock_file_that_cannot_be_made_is_reported_by_its_call)
{
    static char text[MESSAGE_SIZE];
    static char save_path[SAVE_FILE_PATH_SIZE];
    static char expected[MESSAGE_SIZE];
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool named = format_path(save_path, scratch.dir, "/missing/emerald.sav");
    const int status = take_and_capture(save_path, text);
    const bool removed = scratch_remove(&scratch); /* fails if anything was made in it */
    (void)snprintf(expected, sizeof expected, "save_lock: open(%s%s): ", save_path, LOCK_SUFFIX);
    PT_ASSERT(named);
    PT_ASSERT_EQ_INT(status, SAVE_LOCK_ERR);
    PT_ASSERT(strncmp(text, expected, strlen(expected)) == 0); /* then the system's text for ENOENT */
    PT_ASSERT(strlen(text) > 0 && strchr(text, '\n') == text + strlen(text) - 1u); /* one line */
    PT_ASSERT(removed);
}

/* A path of length bytes under <dir>/missing/, so that nothing is made. */
static bool long_save_path(char *path, const char *dir, size_t length)
{
    if (!format_path(path, dir, "/missing/") || strlen(path) >= length)
        return false;
    const size_t start = strlen(path);
    memset(path + start, 'a', length - start);
    path[length] = '\0';
    return true;
}

PTEST(a_save_path_too_long_for_its_lock_file_is_refused_before_any_call)
{
    static char text[MESSAGE_SIZE];
    static char longest[SAVE_FILE_PATH_SIZE];
    static char too_long[SAVE_FILE_PATH_SIZE];
    static char expected[MESSAGE_SIZE];
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool named = long_save_path(longest, scratch.dir, LONGEST_SAVE_PATH)
                       && long_save_path(too_long, scratch.dir, LONGEST_SAVE_PATH + 1u);
    const int refused = take_and_capture(too_long, text);
    (void)snprintf(expected, sizeof expected,
                   "save_lock: the save path %s is too long for its lock file: at most %u bytes\n", too_long,
                   (unsigned)LONGEST_SAVE_PATH);
    const bool refused_as_expected = strcmp(text, expected) == 0;
    const int at_the_limit = take_and_capture(longest, text); /* fits: it gets as far as the open */
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(named);
    PT_ASSERT_EQ_INT(refused, SAVE_LOCK_ERR);
    PT_ASSERT(refused_as_expected);
    PT_ASSERT_EQ_INT(at_the_limit, SAVE_LOCK_ERR);
    PT_ASSERT(strncmp(text, "save_lock: open(", strlen("save_lock: open(")) == 0);
    PT_ASSERT(removed);
}

PTEST(a_release_is_safe_at_any_time_and_taking_another_save_releases_the_first)
{
    static char other_path[SAVE_FILE_PATH_SIZE];
    static char other_lock[SAVE_FILE_PATH_SIZE];
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    save_lock_release(); /* none held */
    save_lock_release();
    const bool named = format_path(other_path, scratch.dir, "/other.sav")
                       && format_path(other_lock, other_path, LOCK_SUFFIX);
    const int first = save_lock_take(scratch.path);
    const int second = save_lock_take(other_path);
    const bool first_freed = free_to_take(scratch.lock);
    const bool second_held = !free_to_take(other_lock);
    save_lock_release();
    save_lock_release();
    const bool second_freed = free_to_take(other_lock);
    const bool other_removed = tsup_remove_file(other_lock);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(named);
    PT_ASSERT_EQ_INT(first, SAVE_LOCK_OK);
    PT_ASSERT_EQ_INT(second, SAVE_LOCK_OK);
    PT_ASSERT(first_freed);
    PT_ASSERT(second_held);
    PT_ASSERT(second_freed);
    PT_ASSERT(other_removed && removed);
}

PTEST_MAIN()
