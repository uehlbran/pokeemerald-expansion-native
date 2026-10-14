/*
 * test_save_lock_posix.c: the save's lock (save_lock.h) between processes,
 * with forked children (posix_child.h): a second instance is refused while a
 * child holds the save; the lock outlives the stores that replace the save (a
 * child's take still fails after them); a child killed with SIGKILL while it
 * holds the lock leaves it free; and the run a restart execs takes the lock
 * again, whether the process released it before the exec (as main does) or
 * the exec closed it (host_file_open's O_CLOEXEC). The rest is in
 * ../test_save_lock.c, the Windows twins in windows/test_save_lock_windows.c.
 * Scratch directories and the save image: ../save_file_fixture.h.
 */
// deps: save_lock.c save_file.c gba_memmap.c flash.c
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "host_process.h"
#include "posix_child.h"
#include "posix_support.h"
#include "save_file_fixture.h"
#include "save_lock.h"

/* The exit codes of the children. */
#define CHILD_TOOK_IT 40      /* save_lock_take: SAVE_LOCK_OK */
#define CHILD_REFUSED 41      /* SAVE_LOCK_HELD */
#define CHILD_FAILED 42       /* SAVE_LOCK_ERR */
#define CHILD_HELD_IT 43      /* the holder took it and held it until it was let go */
#define CHILD_SETUP_FAILED 44 /* something before the take */

/* How long the test waits for a holder to say it holds the lock. */
#define HOLDER_READY_MILLISECONDS 5000

/* Set to the save's path, the restarted process takes its lock (below). */
#define RESTARTED_VARIABLE "SAVE_LOCK_POSIX_TEST_SAVE"

/* What a second instance's start does: takes the save's lock and ends with
 * what it got, its refusal's line sent to the null device. */
static _Noreturn void take_and_exit(const char *save_path)
{
    if (freopen(tsup_null_device(), "w", stderr) == NULL)
        _exit(CHILD_SETUP_FAILED);
    switch (save_lock_take(save_path)) {
    case SAVE_LOCK_OK:
        _exit(CHILD_TOOK_IT);
    case SAVE_LOCK_HELD:
        _exit(CHILD_REFUSED);
    default:
        _exit(CHILD_FAILED);
    }
}

static void take_as_a_second_instance(void *context)
{
    const char *const save_path = context;
    take_and_exit(save_path);
}

/* The run a restart starts, before any test: see RESTARTED_VARIABLE. */
__attribute__((constructor)) static void act_as_the_restarted_process(void)
{
    const char *const save_path = getenv(RESTARTED_VARIABLE);
    if (save_path != NULL)
        take_and_exit(save_path);
}

/* --- a child holding the lock ------------------------------------------------------- */

/* The child takes the lock, writes a byte to ready, and holds the lock until
 * release's end of file (the test closes its end) or a signal ends it. */
struct holder {
    const char *save_path;
    int ready[2];
    int release[2];
    pid_t pid;
};

static void hold_the_lock(void *context)
{
    const struct holder *const holder = context;
    (void)close(holder->ready[0]);
    (void)close(holder->release[1]);
    if (save_lock_take(holder->save_path) != SAVE_LOCK_OK)
        _exit(CHILD_FAILED);
    const char byte = 1;
    if (write(holder->ready[1], &byte, 1) != 1)
        _exit(CHILD_SETUP_FAILED);
    char ignored;
    ssize_t got;
    do
        got = read(holder->release[0], &ignored, 1);
    while (got > 0);
    _exit(CHILD_HELD_IT);
}

/* Starts the holder; true once it holds the lock. let_holder_go or
 * kill_holder ends it either way. */
static bool start_holder(struct holder *holder, const char *save_path)
{
    holder->save_path = save_path;
    holder->pid = -1;
    holder->ready[0] = holder->ready[1] = holder->release[0] = holder->release[1] = -1;
    if (pipe(holder->ready) != 0 || pipe(holder->release) != 0)
        return false;
    holder->pid = psup_start_child_running(hold_the_lock, holder);
    (void)close(holder->ready[1]);
    (void)close(holder->release[0]);
    holder->ready[1] = holder->release[0] = -1;
    char byte = 0;
    return holder->pid > 0 && psup_read_within(holder->ready[0], &byte, 1, HOLDER_READY_MILLISECONDS) == 1;
}

/* Closes the test's ends of the pipes (the end of file the holder waits for)
 * and waits for the holder; its wait status. */
static int let_holder_go(struct holder *holder)
{
    for (size_t i = 0; i < 2; i++) {
        if (holder->ready[i] >= 0)
            (void)close(holder->ready[i]);
        if (holder->release[i] >= 0)
            (void)close(holder->release[i]);
    }
    return holder->pid > 0 ? psup_wait_child(holder->pid) : -1;
}

/* Kills the holder with SIGKILL, holding the lock, and waits for it. */
static int kill_holder(struct holder *holder)
{
    if (holder->pid > 0)
        (void)kill(holder->pid, SIGKILL);
    return let_holder_go(holder);
}

/* --- the tests ------------------------------------------------------------------------ */

struct take {
    const char *save_path;
    int status;
};

static void take_the_lock(void *context)
{
    struct take *const take = context;
    take->status = save_lock_take(take->save_path);
}

PTEST(a_second_instance_is_refused_while_a_child_holds_the_save)
{
    static char text[3u * SAVE_FILE_PATH_SIZE];
    static char expected[3u * SAVE_FILE_PATH_SIZE];
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    struct holder holder;
    const bool held = start_holder(&holder, scratch.path);
    struct take take = {.save_path = scratch.path, .status = SAVE_LOCK_OK};
    const bool captured = tsup_capture_stderr(take_the_lock, &take, text, sizeof text);
    const int status = let_holder_go(&holder);
    const int taken_after = save_lock_take(scratch.path); /* the holder's end released it */
    save_lock_release();
    const bool removed = scratch_remove(&scratch);
    (void)snprintf(expected, sizeof expected,
                   "save_lock: another instance of the game holds the save %s (the lock on %s); not starting, so "
                   "that the two cannot write over each other's saves\n",
                   scratch.path, scratch.lock);
    PT_ASSERT(held);
    PT_ASSERT(captured);
    PT_ASSERT_EQ_INT(take.status, SAVE_LOCK_HELD);
    PT_ASSERT_EQ_STR(text, expected);
    PT_ASSERT_EQ_INT(psup_exit_code(status), CHILD_HELD_IT);
    PT_ASSERT_EQ_INT(taken_after, SAVE_LOCK_OK);
    PT_ASSERT(removed);
}

/* Each store replaces the save with a new file; the lock is on <save>.lock,
 * which no store touches. */
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
    const int while_held = psup_exit_code(psup_run_child(take_as_a_second_instance, scratch.path));
    save_lock_release();
    const int after_release = psup_exit_code(psup_run_child(take_as_a_second_instance, scratch.path));
    const bool saved = read_file(scratch.path, FILE_BYTES);
    save_file_shutdown();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(taken, SAVE_LOCK_OK);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(first, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(second, SAVE_FILE_OK);
    PT_ASSERT(saved);
    PT_ASSERT_EQ_INT(while_held, CHILD_REFUSED);
    PT_ASSERT_EQ_INT(after_release, CHILD_TOOK_IT);
    PT_ASSERT(removed);
}

/* A crash or a kill ends the process without a release: the system drops the
 * lock with the process. */
PTEST(a_holder_killed_with_sigkill_leaves_the_lock_free)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    struct holder holder;
    const bool held = start_holder(&holder, scratch.path);
    const int status = kill_holder(&holder);
    const int taken = save_lock_take(scratch.path);
    save_lock_release();
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(held);
    PT_ASSERT_EQ_INT(psup_terminating_signal(status), SIGKILL);
    PT_ASSERT_EQ_INT(taken, SAVE_LOCK_OK);
    PT_ASSERT(removed);
}

/* --- the restart --------------------------------------------------------------------- */

/* In the child: the lock taken, and the variable that makes the restarted
 * run take it again (act_as_the_restarted_process). */
static void take_before_the_restart(const char *save_path)
{
    if (!tsup_set_environment(RESTARTED_VARIABLE, save_path) || save_lock_take(save_path) != SAVE_LOCK_OK)
        _exit(CHILD_SETUP_FAILED);
}

static _Noreturn void restart_this_executable(void)
{
    char *const argv[] = {"test_save_lock_posix", NULL};
    (void)host_process_restart(argv); /* returns only on failure */
    _exit(CHILD_SETUP_FAILED);
}

static void release_and_restart(void *context)
{
    const char *const save_path = context;
    take_before_the_restart(save_path);
    save_lock_release();
    restart_this_executable();
}

static void restart_holding_it(void *context)
{
    const char *const save_path = context;
    take_before_the_restart(save_path);
    restart_this_executable();
}

PTEST(the_run_a_restart_starts_takes_the_lock_again)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    /* Released before the exec, as main does; and still held at the exec,
     * which closes it (O_CLOEXEC). */
    const int released = psup_exit_code(psup_run_child(release_and_restart, scratch.path));
    const int held = psup_exit_code(psup_run_child(restart_holding_it, scratch.path));
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(released, CHILD_TOOK_IT);
    PT_ASSERT_EQ_INT(held, CHILD_TOOK_IT);
    PT_ASSERT(removed);
}

PTEST_MAIN()
