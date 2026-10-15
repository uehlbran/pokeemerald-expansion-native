// deps: console.c data_dir.c
/*
 * test_host_console_windows.c: what the standard streams are and the parent's
 * console (host_console.h) on Windows. This executable started again as a
 * child (windows_support.h names its body) with the standard handles the test
 * gives it: a pipe, a file, the null device, none at all, and the
 * process-creation flags that decide whether it has a console of its own, so
 * that the cases of a GUI program's start are the cases the child sees:
 * Explorer's (no handles, no console), a prompt's (no handles, a parent with a
 * console) and a harness's (pipes). A child ends with a code that says what
 * host_console.h told it.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "console.h"
#include "data_dir.h"
#include "host_console.h"
#include "ptest.h"
#include "test_support.h"
#include "windows_support.h"

#define CHILD_KINDS "kinds"
#define CHILD_ATTACH "attach"
#define CHILD_MIDDLE "middle"
#define CHILD_LOG "log"

/* Names the data directory the "log" child puts its log in. */
#define DIR_VARIABLE "HOST_CONSOLE_WINDOWS_TEST_DIR"

/* The codes the "attach" child ends with. */
#define ATTACH_REFUSED 20u
#define ATTACH_WROTE 21u
#define ATTACH_OUT_FAILED 22u
#define ATTACH_ERR_FAILED 24u
#define CHILD_FAILED 23u

/* The codes the "log" child ends with. */
#define LOG_WRITTEN 30u
#define LOG_NOT_OPENED 31u
#define LOG_WRITE_FAILED 33u

/* How long a child may take. */
#define CHILD_MILLISECONDS 20000u

/* "kinds" ends with the kind of standard output times this plus the kind of
 * standard error. */
#define KIND_RADIX 4u

/* --- the children -------------------------------------------------------------- */

static unsigned kinds_of_this_process(void)
{
    return (unsigned)host_console_stream_kind(HOST_STREAM_OUT) * KIND_RADIX
           + (unsigned)host_console_stream_kind(HOST_STREAM_ERR);
}

/* Attaches to the parent's console and writes on both streams. */
static unsigned attach_and_write(void)
{
    if (!host_console_attach_parent())
        return ATTACH_REFUSED;
    if (fputs("console test: standard output\n", stdout) < 0 || fflush(stdout) != 0)
        return ATTACH_OUT_FAILED;
    if (fputs("console test: standard error\n", stderr) < 0 || fflush(stderr) != 0)
        return ATTACH_ERR_FAILED;
    return ATTACH_WROTE;
}

static bool run_self(const char *body, HANDLE out, HANDLE err, DWORD flags, uint32_t *code);

/* What a game started by Explorer does with LOG mode: opens its log on the
 * standard streams, which it does not have, and prints through the C
 * runtime's streams and through stderr's descriptor, where a crash report goes. */
static unsigned open_the_log_and_write(void)
{
    static const char raw[] = "through stderr's descriptor\n";
    struct options options = {.scale = OPTIONS_DEFAULT_SCALE, .data_dir = getenv(DIR_VARIABLE)};
    const struct console_streams streams = console_standard_streams();
    char path[DATA_DIR_PATH_SIZE];
    if (options.data_dir == NULL || console_open_log(&options, data_dir_sdl_host(), &streams, path, sizeof path) != 0)
        return LOG_NOT_OPENED;
    if (fputs("through stdout\n", stdout) < 0 || fputs("through stderr\n", stderr) < 0
        || _write(_fileno(stderr), raw, sizeof raw - 1u) != (int)(sizeof raw - 1u))
        return LOG_WRITE_FAILED;
    return LOG_WRITTEN;
}

/* The child that starts the "attach" child as Explorer would start a program:
 * no standard handles, no console of its own, with this process as its parent. */
static unsigned start_the_attacher(void)
{
    uint32_t code = CHILD_FAILED;
    const bool ran = run_self(CHILD_ATTACH, NULL, NULL, DETACHED_PROCESS, &code);
    return ran ? code : CHILD_FAILED;
}

/* --- starting this executable again ----------------------------------------------- */

/* Starts this executable with the body named, standard output and error the
 * handles given (NULL for both: none are passed on), and the creation flags;
 * waits for it and writes its exit code to *code. out and err must be
 * inheritable. */
static bool run_self(const char *body, HANDLE out, HANDLE err, DWORD flags, uint32_t *code)
{
    char path[TSUP_PATH_SIZE];
    const DWORD length = GetModuleFileNameA(NULL, path, sizeof path);
    if (length == 0 || length >= sizeof path || !tsup_set_environment(WSUP_CHILD_VARIABLE, body))
        return false;
    char command_line[TSUP_PATH_SIZE + 2u];
    if (snprintf(command_line, sizeof command_line, "\"%s\"", path) >= (int)sizeof command_line)
        return false;
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    const bool passes_handles = out != NULL;
    if (passes_handles) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = out;
        startup.hStdError = err;
        startup.hStdInput = NULL;
    }
    PROCESS_INFORMATION child;
    const bool started = CreateProcessA(path, command_line, NULL, NULL, passes_handles, flags, NULL, NULL, &startup,
                                        &child);
    const bool unset = tsup_set_environment(WSUP_CHILD_VARIABLE, "");
    if (!started)
        return false;
    (void)CloseHandle(child.hThread);
    bool ended = WaitForSingleObject(child.hProcess, CHILD_MILLISECONDS) == WAIT_OBJECT_0;
    if (!ended)
        (void)TerminateProcess(child.hProcess, CHILD_FAILED);
    DWORD exit_code = CHILD_FAILED;
    ended = GetExitCodeProcess(child.hProcess, &exit_code) && ended;
    (void)CloseHandle(child.hProcess);
    *code = exit_code;
    return ended && unset;
}

/* An inheritable handle on the null device, for writing. */
static HANDLE open_null_device(void)
{
    SECURITY_ATTRIBUTES inheritable = {.nLength = sizeof inheritable, .bInheritHandle = TRUE};
    return CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

static unsigned expected_kinds(enum host_stream_kind out, enum host_stream_kind err)
{
    return (unsigned)out * KIND_RADIX + (unsigned)err;
}

/* --- the tests ------------------------------------------------------------------- */

PTEST(a_pipe_is_a_pipe_and_a_file_is_a_file)
{
    SECURITY_ATTRIBUTES inheritable = {.nLength = sizeof inheritable, .bInheritHandle = TRUE};
    HANDLE read_end = NULL;
    HANDLE write_end = NULL;
    PT_ASSERT(CreatePipe(&read_end, &write_end, &inheritable, 0));
    char path[TSUP_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_file(path, sizeof path, "console_file"));
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    PT_ASSERT(file != INVALID_HANDLE_VALUE);
    uint32_t pipes = 0;
    uint32_t files = 0;
    uint32_t mixed = 0;
    const bool ran = run_self(CHILD_KINDS, write_end, write_end, CREATE_NO_WINDOW, &pipes)
                     && run_self(CHILD_KINDS, file, file, CREATE_NO_WINDOW, &files)
                     && run_self(CHILD_KINDS, write_end, file, CREATE_NO_WINDOW, &mixed);
    (void)CloseHandle(file);
    (void)CloseHandle(read_end);
    (void)CloseHandle(write_end);
    PT_ASSERT(tsup_remove_file(path));
    PT_ASSERT(ran);
    PT_ASSERT_EQ_U32(pipes, expected_kinds(HOST_STREAM_PIPE, HOST_STREAM_PIPE));
    PT_ASSERT_EQ_U32(files, expected_kinds(HOST_STREAM_FILE, HOST_STREAM_FILE));
    PT_ASSERT_EQ_U32(mixed, expected_kinds(HOST_STREAM_PIPE, HOST_STREAM_FILE));
}

/* The null device is a character device, and not a console this process writes to. */
PTEST(the_null_device_is_nothing_and_a_pipe_beside_it_is_still_a_pipe)
{
    SECURITY_ATTRIBUTES inheritable = {.nLength = sizeof inheritable, .bInheritHandle = TRUE};
    HANDLE read_end = NULL;
    HANDLE write_end = NULL;
    PT_ASSERT(CreatePipe(&read_end, &write_end, &inheritable, 0));
    HANDLE null_device = open_null_device();
    PT_ASSERT(null_device != INVALID_HANDLE_VALUE);
    uint32_t both = 0;
    uint32_t one = 0;
    const bool ran = run_self(CHILD_KINDS, null_device, null_device, CREATE_NO_WINDOW, &both)
                     && run_self(CHILD_KINDS, write_end, null_device, CREATE_NO_WINDOW, &one);
    (void)CloseHandle(null_device);
    (void)CloseHandle(read_end);
    (void)CloseHandle(write_end);
    PT_ASSERT(ran);
    PT_ASSERT_EQ_U32(both, expected_kinds(HOST_STREAM_NONE, HOST_STREAM_NONE));
    PT_ASSERT_EQ_U32(one, expected_kinds(HOST_STREAM_PIPE, HOST_STREAM_NONE));
}

/* Explorer's start: no handles passed, no console. */
PTEST(a_process_started_with_no_handles_and_no_console_has_no_streams)
{
    uint32_t code = 0;
    PT_ASSERT(run_self(CHILD_KINDS, NULL, NULL, DETACHED_PROCESS, &code));
    PT_ASSERT_EQ_U32(code, expected_kinds(HOST_STREAM_NONE, HOST_STREAM_NONE));
}

/* A prompt's start of a program without a console of its own: it has the
 * prompt's console behind it, and takes it as its streams. */
PTEST(a_process_with_a_console_has_console_streams)
{
    uint32_t code = 0;
    PT_ASSERT(run_self(CHILD_KINDS, NULL, NULL, CREATE_NO_WINDOW, &code));
    PT_ASSERT_EQ_U32(code, expected_kinds(HOST_STREAM_CONSOLE, HOST_STREAM_CONSOLE));
}

/* The middle process has a console (CREATE_NO_WINDOW makes one without a window), and starts
 * the attacher without. */
PTEST(attaching_to_the_parents_console_works_and_the_streams_write_on_it)
{
    uint32_t code = 0;
    PT_ASSERT(run_self(CHILD_MIDDLE, NULL, NULL, CREATE_NO_WINDOW, &code));
    PT_ASSERT_EQ_U32(code, ATTACH_WROTE);
}

/* Every way of writing to the standard streams reaches the log. */
PTEST(a_process_with_no_handles_and_no_console_logs_through_every_way_of_writing)
{
    char dir[TSUP_PATH_SIZE];
    char path[TSUP_PATH_SIZE + 32u];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "console_log"));
    PT_ASSERT(tsup_set_environment(DIR_VARIABLE, dir));
    uint32_t code = 0;
    const bool ran = run_self(CHILD_LOG, NULL, NULL, DETACHED_PROCESS, &code);
    (void)snprintf(path, sizeof path, "%s/%s", dir, CONSOLE_LOG_NAME);
    char text[256] = "";
    FILE *const log = fopen(path, "rb");
    size_t length = 0;
    if (log != NULL) {
        int c;
        while ((c = fgetc(log)) != EOF && length + 1u < sizeof text) {
            if (c != '\r')
                text[length++] = (char)c;
        }
        (void)fclose(log);
    }
    text[length] = '\0';
    PT_ASSERT(tsup_unset_environment(DIR_VARIABLE));
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT(ran);
    PT_ASSERT_EQ_U32(code, LOG_WRITTEN);
    PT_ASSERT_EQ_STR(text, "through stdout\nthrough stderr\nthrough stderr's descriptor\n");
}

PTEST(attaching_when_the_parent_has_no_console_is_refused)
{
    uint32_t code = 0;
    PT_ASSERT(run_self(CHILD_MIDDLE, NULL, NULL, DETACHED_PROCESS, &code));
    PT_ASSERT_EQ_U32(code, ATTACH_REFUSED);
}

int main(void)
{
    const char *const name = wsup_child_name();
    if (name == NULL)
        return ptest_main_for(&ptest_tests, stdout);
    if (strcmp(name, CHILD_KINDS) == 0)
        return (int)kinds_of_this_process();
    if (strcmp(name, CHILD_ATTACH) == 0)
        return (int)attach_and_write();
    if (strcmp(name, CHILD_MIDDLE) == 0)
        return (int)start_the_attacher();
    if (strcmp(name, CHILD_LOG) == 0)
        return (int)open_the_log_and_write();
    return (int)WSUP_CHILD_RETURNED;
}
