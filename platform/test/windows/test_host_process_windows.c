/*
 * test_host_process_windows.c: the restart on Windows (host_process.h), which
 * has no exec: a child (windows_support.h) restarts this very executable
 * with arguments the command line must quote (spaces, quotes, backslashes,
 * an empty one) and a variable added to its environment; the restarted
 * process, seeing RESTARTED_MODE as argv[1], writes the arguments it got to
 * the file the variable names and ends with RESTARTED_CODE, and the child,
 * which waits for it, must end with that code too. The child also holds an
 * inheritable semaphore, which the restarted process must not have: it
 * inherits the standard handles and nothing else.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "host_process.h"
#include "ptest.h"
#include "test_support.h"
#include "windows_support.h"

#define FILE_VARIABLE "HOST_PROCESS_WINDOWS_TEST_FILE"
#define HANDLE_VARIABLE "HOST_PROCESS_WINDOWS_TEST_HANDLE"
#define RESTARTED_MODE "--restarted"
#define RESTARTED_CODE 42
#define RESTART_FAILED_CODE 7
#define WRITE_FAILED_CODE 8
#define HANDLE_INHERITED_CODE 9
#define TEXT_SIZE 512u

/* The semaphore the child holds, told apart from any other object by its
 * count: a ReleaseSemaphore through the same handle value in the restarted
 * process finds this count only if the handle was inherited. */
#define MARKER_COUNT 7
#define MARKER_MAXIMUM 9
#define HANDLE_TEXT_SIZE 32u

/* True if the handle value the child put in HANDLE_VARIABLE is, in this
 * process, the child's semaphore. */
static bool holds_the_childs_semaphore(void)
{
    const char *const text = getenv(HANDLE_VARIABLE);
    if (text == NULL)
        return false;
    const uintptr_t value = (uintptr_t)strtoull(text, NULL, 10);
    LONG previous = 0;
    return ReleaseSemaphore((HANDLE)value, 1, &previous) && previous == MARKER_COUNT; /* the value is a handle */
}

/* What the child restarts with, after argv[0]: each argument a quoting rule
 * of the C runtime's parser must carry through. */
static const char *const restart_arguments[] = {
    RESTARTED_MODE,
    "plain",
    "with spaces",
    "",
    "a \"quoted\" word",
    "trailing backslash\\",
    "back\\\\slashes \\\"before a quote",
    "C:\\a path\\",
};
#define RESTART_ARGUMENT_COUNT (sizeof restart_arguments / sizeof restart_arguments[0])

/* The restarted process: its arguments, one per line, to the file. */
static int act_as_the_restarted_process(int argc, char **argv)
{
    const char *const path = getenv(FILE_VARIABLE);
    FILE *const file = path != NULL ? fopen(path, "wb") : NULL;
    if (file == NULL)
        return WRITE_FAILED_CODE;
    for (int i = 1; i < argc; i++)
        (void)fprintf(file, "[%s]\n", argv[i]);
    if (fclose(file) != 0)
        return WRITE_FAILED_CODE;
    return holds_the_childs_semaphore() ? HANDLE_INHERITED_CODE : RESTARTED_CODE;
}

/* The child: restarts this executable; returns only if that failed. */
static void restart_this_executable(void)
{
    static char *argv[RESTART_ARGUMENT_COUNT + 2];
    argv[0] = "test_host_process_windows";
    for (size_t i = 0; i < RESTART_ARGUMENT_COUNT; i++)
        argv[i + 1] = (char *)restart_arguments[i];
    argv[RESTART_ARGUMENT_COUNT + 1] = NULL;
    SECURITY_ATTRIBUTES inheritable = {.nLength = sizeof inheritable, .bInheritHandle = TRUE};
    HANDLE marker = CreateSemaphoreA(&inheritable, MARKER_COUNT, MARKER_MAXIMUM, NULL);
    char text[HANDLE_TEXT_SIZE];
    (void)snprintf(text, sizeof text, "%llu", (unsigned long long)(uintptr_t)marker); /* the handle's value */
    if (marker == NULL || !tsup_set_environment(HANDLE_VARIABLE, text))
        wsup_end(RESTART_FAILED_CODE);
    (void)host_process_restart(argv);
    wsup_end(RESTART_FAILED_CODE);
}

PTEST(restart_starts_this_executable_with_the_arguments_and_environment_and_ends_with_its_code)
{
    char path[TSUP_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_file(path, sizeof path, "test_host_process_windows"));
    const bool set = tsup_set_environment(FILE_VARIABLE, path);
    uint32_t code = 0;
    const bool ran = set && wsup_run_child("restart", &code);
    const bool unset = tsup_unset_environment(FILE_VARIABLE);
    char text[TEXT_SIZE] = "";
    FILE *const file = fopen(path, "rb");
    if (file != NULL) {
        text[fread(text, 1, sizeof text - 1u, file)] = '\0';
        (void)fclose(file);
    }
    const bool removed = tsup_remove_file(path);
    char expected[TEXT_SIZE] = "";
    for (size_t i = 0; i < RESTART_ARGUMENT_COUNT; i++) {
        const size_t used = strlen(expected);
        (void)snprintf(expected + used, sizeof expected - used, "[%s]\n", restart_arguments[i]);
    }
    PT_ASSERT(set && unset && removed);
    PT_ASSERT(ran);
    PT_ASSERT_EQ_U32(code, RESTARTED_CODE);
    PT_ASSERT_EQ_STR(text, expected);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], RESTARTED_MODE) == 0)
        return act_as_the_restarted_process(argc, argv);
    const char *const name = wsup_child_name();
    if (name != NULL && strcmp(name, "restart") == 0)
        restart_this_executable();
    if (name != NULL)
        return (int)WSUP_CHILD_RETURNED;
    return ptest_main_for(&ptest_tests, stdout);
}
