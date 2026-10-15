/*
 * host_console.h: what the process's standard output and error are, and the
 * console of the process that started it.
 *
 * Not hardware: on the GBA nothing printed. The game prints (the test
 * runner's results, the replays' output, the refusals), and on Windows the
 * executable is a GUI program, which Explorer starts without a console
 * window (docs/DECISIONS.md, "No console window on Windows"); console.h
 * decides from these answers where its messages go.
 *
 * Wraps, on Windows (host_windows_console.c): GetStdHandle, GetFileType and
 * GetConsoleMode for what a stream is, AttachConsole(ATTACH_PARENT_PROCESS)
 * and freopen on "CONOUT$" to print on the prompt that started the game. On
 * POSIX (host_posix.c) the C library's streams are already the terminal, the
 * pipe or the file the parent gave, so every stream is reported as given and
 * nothing is attached.
 */
#ifndef PLATFORM_HOST_CONSOLE_H
#define PLATFORM_HOST_CONSOLE_H

#include <stdbool.h>
#include <stdio.h>

enum host_stream {
    HOST_STREAM_OUT, /* standard output */
    HOST_STREAM_ERR  /* standard error */
};

/* What a stream is connected to, as the system reports it. */
enum host_stream_kind {
    HOST_STREAM_NONE, /* nothing: no handle, or a handle that cannot be written to (a GUI program started by Explorer) */
    HOST_STREAM_PIPE,    /* a pipe the parent gave (bash, the hydra) */
    HOST_STREAM_FILE,    /* a file the parent gave (`> file`) */
    HOST_STREAM_CONSOLE, /* a console this process is attached to */
};

enum host_stream_kind host_console_stream_kind(enum host_stream which);

/* Attaches this process to its parent's console, if the parent has one, and
 * reopens every stream that is HOST_STREAM_NONE on it; streams that are
 * something else are left as they are. Returns true if it did. Returns false,
 * with the process and its streams as they were, if the parent has no console
 * (Explorer, a service, a detached launcher) or a stream could not be
 * reopened. Never called on POSIX systems, where no stream is NONE. */
bool host_console_attach_parent(void);

/* Points the stream at the null device, so that what is written to it before
 * its real destination is known goes nowhere instead of to a descriptor that
 * may not exist. Returns 0, or -1 after a report on stderr (the stream is then
 * closed). POSIX: /dev/null; Windows: NUL. */
int host_console_discard_stream(FILE *stream);

/* Makes target write where source writes: target's descriptor becomes a
 * duplicate of source's, so that the two are one open file with one offset,
 * which a fault handler's raw write to either continues rather than
 * overwrites. Flushes target first. Returns 0, or -1 after a report on
 * stderr. POSIX: dup2; Windows: _dup2, whose duplicate shares the file pointer. */
int host_console_share_stream(FILE *source, FILE *target);

#endif /* PLATFORM_HOST_CONSOLE_H */
