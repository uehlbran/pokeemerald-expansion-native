/*
 * host_windows_console.c: host_console.h on Windows (x86-64, MinGW-w64): what
 * the standard streams are and the parent's console.
 *
 * Followed: Microsoft's documentation of GetStdHandle (processenv.h),
 * GetFileType (fileapi.h), GetConsoleMode, AttachConsole and FreeConsole
 * (consoleapi.h, wincon.h), "Creation of a console" (a process started with
 * the GUI subsystem has none, and AttachConsole joins its parent's), and the
 * C runtime's freopen. A GUI process gets the handles its parent put in
 * STARTUPINFO, or none; the C runtime takes stdout and stderr from them when
 * it starts, so a pipe or a file given by the parent needs nothing here.
 */
#include <stdbool.h>
#include <stdio.h>

#include <windows.h>

#include "host_console.h"

/* The console's output device, for fopen and freopen (consoleapi, "Console
 * Handles": "CONOUT$" is the active screen buffer). */
#define CONSOLE_OUTPUT_DEVICE "CONOUT$"

static DWORD handle_name(enum host_stream which)
{
    return which == HOST_STREAM_OUT ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
}

enum host_stream_kind host_console_stream_kind(enum host_stream which)
{
    HANDLE handle = GetStdHandle(handle_name(which));
    if (handle == NULL || handle == INVALID_HANDLE_VALUE)
        return HOST_STREAM_NONE;
    switch (GetFileType(handle)) {
    case FILE_TYPE_DISK:
        return HOST_STREAM_FILE;
    case FILE_TYPE_PIPE:
        return HOST_STREAM_PIPE;
    case FILE_TYPE_CHAR: {
        /* The null device is a character device too, and so is a console
         * handle inherited by a process that is not attached to it; only
         * a handle with a console mode is a console this process writes to. */
        DWORD mode;
        return GetConsoleMode(handle, &mode) ? HOST_STREAM_CONSOLE : HOST_STREAM_NONE;
    }
    default:
        return HOST_STREAM_NONE;
    }
}

static FILE *stream_of(enum host_stream which)
{
    return which == HOST_STREAM_OUT ? stdout : stderr;
}

bool host_console_attach_parent(void)
{
    static const enum host_stream streams[] = {HOST_STREAM_OUT, HOST_STREAM_ERR};
    /* Asked before the attach: joining a console may give the process
     * standard handles (the C runtime's descriptors stay as they were), and
     * a stream is mended by what it was, not by what the handles are after. */
    bool missing[sizeof streams / sizeof streams[0]];
    for (size_t i = 0; i < sizeof streams / sizeof streams[0]; i++)
        missing[i] = host_console_stream_kind(streams[i]) == HOST_STREAM_NONE;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return false;
    for (size_t i = 0; i < sizeof streams / sizeof streams[0]; i++) {
        if (!missing[i])
            continue;
        FILE *const stream = stream_of(streams[i]);
        // cppcheck-suppress leakReturnValNotUsed ; freopen gives back the stream it was given, which stays stdout or stderr
        if (freopen(CONSOLE_OUTPUT_DEVICE, "w", stream) == NULL || setvbuf(stream, NULL, _IONBF, 0) != 0) {
            (void)FreeConsole();
            return false;
        }
    }
    return true;
}
