/*
 * test_save_lock_windows.c: the save's lock (save_lock.h) between processes
 * on Windows (LockFileEx), with this executable started again as a child
 * (windows_support.h), since Windows has no fork; the twins of
 * posix/test_save_lock_posix.c. A child, a second instance, is refused while
 * this process holds the save, and takes it once it is released; the lock
 * outlives the stores that replace the save; a child that takes the lock and
 * is ended at once (TerminateProcess, which is how a kill ends a process)
 * leaves it free; and the run a restart starts (host_process_restart: a child
 * the restarting process waits for) takes the lock its parent released
 * first, as main does. The rest is in ../test_save_lock.c. Scratch
 * directories and the save image: ../save_file_fixture.h.
 */
// deps: save_lock.c save_file.c gba_memmap.c flash.c
#include <stdlib.h>
#include <string.h>

#include "host_process.h"
#include "save_file_fixture.h"
#include "save_lock.h"
#include "windows_support.h"

/* The exit codes of the children. */
#define CHILD_TOOK_IT 40u       /* save_lock_take: SAVE_LOCK_OK */
#define CHILD_REFUSED 41u       /* SAVE_LOCK_HELD */
#define CHILD_FAILED 42u        /* SAVE_LOCK_ERR */
#define CHILD_ENDED_HOLDING 43u /* took it and ended at once, holding it */
#define CHILD_SETUP_FAILED 44u  /* something before the take */

/* The save's path, for the children, which inherit the environment. */
#define SAVE_VARIABLE "SAVE_LOCK_WINDOWS_TEST_SAVE"

/* argv[1] of the run a restart starts. */
#define RESTARTED_MODE "--restarted"

/* --- the children ---------------------------------------------------------------------- */

static const char *save_path_given(void)
{
    const char *const path = getenv(SAVE_VARIABLE);
    if (path == NULL || path[0] == '\0')
        wsup_end(CHILD_SETUP_FAILED);
    return path;
}

/* What a second instance's start does: takes the save's lock and ends with
 * what it got, its refusal's line sent to the null device. */
static _Noreturn void take_and_end(void)
{
    const char *const save_path = save_path_given();
    if (freopen(tsup_null_device(), "w", stderr) == NULL)
        wsup_end(CHILD_SETUP_FAILED);
    switch (save_lock_take(save_path)) {
    case SAVE_LOCK_OK:
        wsup_end(CHILD_TOOK_IT);
    case SAVE_LOCK_HELD:
        wsup_end(CHILD_REFUSED);
    default:
        wsup_end(CHILD_FAILED);
    }
}

/* Takes the lock and ends the process at once, without releasing it. */
static _Noreturn void take_and_end_holding(void)
{
    wsup_end(save_lock_take(save_path_given()) == SAVE_LOCK_OK ? CHILD_ENDED_HOLDING : CHILD_FAILED);
}

/* Takes the lock, releases it and restarts, as main does: the restarted run,
 * a child of this one, which waits for it and ends with its code, takes it
 * again (RESTARTED_MODE). */
static _Noreturn void take_release_and_restart(void)
{
    char *argv[] = {"test_save_lock_windows", RESTARTED_MODE, NULL};
    if (save_lock_take(save_path_given()) != SAVE_LOCK_OK)
        wsup_end(CHILD_SETUP_FAILED);
    save_lock_release();
    (void)host_process_restart(argv); /* returns only on failure */
    wsup_end(CHILD_SETUP_FAILED);
}

/* The child named name, with this test's save, and the code it ended with. */
static bool run_child(const char *name, const char *save_path, uint32_t *code)
{
    *code = 0;
    const bool set = tsup_set_environment(SAVE_VARIABLE, save_path);
    const bool ran = set && wsup_run_child(name, code);
    return tsup_unset_environment(SAVE_VARIABLE) && ran;
}

/* --- the tests ------------------------------------------------------------------------- */

PTEST(a_second_instance_is_refused_while_this_one_holds_the_save)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int taken = save_lock_take(scratch.path);
    uint32_t while_held = 0;
    const bool ran = run_child("take", scratch.path, &while_held);
    save_lock_release();
    uint32_t after_release = 0;
    const bool ran_again = run_child("take", scratch.path, &after_release);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(taken, SAVE_LOCK_OK);
    PT_ASSERT(ran && ran_again);
    PT_ASSERT_EQ_U32(while_held, CHILD_REFUSED);
    PT_ASSERT_EQ_U32(after_release, CHILD_TOOK_IT);
    PT_ASSERT(removed);
}

PTEST(the_lock_outlives_the_stores_that_replace_the_save)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int taken = save_lock_take(scratch.path);
    set_image(0x31);
    const int init = save_file_init(scratch.path);
    const int first = save_file_store();
    set_image(0x32);
    const int second = save_file_store();
    uint32_t while_held = 0;
    const bool ran = run_child("take", scratch.path, &while_held);
    save_lock_release();
    const bool saved = read_file(scratch.path, FILE_BYTES);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(taken, SAVE_LOCK_OK);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(second, SAVE_FILE_OK);
    PT_ASSERT(saved);
    PT_ASSERT(ran);
    PT_ASSERT_EQ_U32(while_held, CHILD_REFUSED);
    PT_ASSERT(removed);
}

PTEST(a_holder_ended_at_once_leaves_the_lock_free)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    uint32_t code = 0;
    const bool ran = run_child("end-holding", scratch.path, &code);
    const int taken = save_lock_take(scratch.path);
    save_lock_release();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(ran);
    PT_ASSERT_EQ_U32(code, CHILD_ENDED_HOLDING);
    PT_ASSERT_EQ_INT(taken, SAVE_LOCK_OK);
    PT_ASSERT(removed);
}

PTEST(the_run_a_restart_starts_takes_the_lock_its_parent_released)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    uint32_t code = 0;
    const bool ran = run_child("restart", scratch.path, &code);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(ran);
    PT_ASSERT_EQ_U32(code, CHILD_TOOK_IT);
    PT_ASSERT(removed);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], RESTARTED_MODE) == 0)
        take_and_end();
    const char *const name = wsup_child_name();
    if (name != NULL && strcmp(name, "take") == 0)
        take_and_end();
    if (name != NULL && strcmp(name, "end-holding") == 0)
        take_and_end_holding();
    if (name != NULL && strcmp(name, "restart") == 0)
        take_release_and_restart();
    if (name != NULL)
        return (int)WSUP_CHILD_RETURNED;
    return ptest_main_for(&ptest_tests, stdout);
}
