/*
 * windows_support.h: what the tests in test/windows/ share, on top of
 * test_support.h: this test executable started again as a child process
 * that runs one named body instead of the tests, and the code it ends with
 * (Windows has no fork; posix/posix_support.h is the POSIX counterpart).
 * Implemented in support.c, beside this file. Tests of any other family do
 * not include it.
 *
 * A test file with children defines main itself: wsup_child_name() names
 * the body to run, else it runs the tests (ptest_main_for).
 */
#ifndef PLATFORM_TEST_WINDOWS_SUPPORT_H
#define PLATFORM_TEST_WINDOWS_SUPPORT_H

#include <stdbool.h>
#include <stdint.h>

/* The environment variable that names a child's body. */
#define WSUP_CHILD_VARIABLE "PTEST_WINDOWS_CHILD"

/* How long a child may take before it is ended and the test fails, so that a
 * loop that would never end (a handler that resumes at the fault again)
 * fails the test instead of hanging it. */
#define WSUP_CHILD_MILLISECONDS 10000u

/* The code of a child whose body returned instead of ending the process. */
#define WSUP_CHILD_RETURNED 3u

/* Starts this executable again with WSUP_CHILD_VARIABLE set to name, on the
 * same standard handles, waits for it, and writes the code it ended with to
 * *code: its exit code, or the exception code a fault ended it with. False if
 * it could not be started, or did not end in time (it is ended then). */
bool wsup_run_child(const char *name, uint32_t *code);

/* The body this process was started to run (wsup_run_child), or NULL. */
const char *wsup_child_name(void);

/* Ends this process at once with code: no clean-up, no flush (as _exit on
 * POSIX), from anywhere, a fault handler included. */
_Noreturn void wsup_end(uint32_t code);

#endif /* PLATFORM_TEST_WINDOWS_SUPPORT_H */
