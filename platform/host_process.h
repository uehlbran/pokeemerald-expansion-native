/*
 * host_process.h: the running executable's own file, and starting it again.
 *
 * Stands in for the console's power cycle and the BIOS's reset after a
 * crash: restart (the replay's switch-off and switch-on) and crash_reboot
 * start the same program again in the same process; symbols finds the
 * function map written next to the executable.
 *
 * Wraps, on Linux: /proc/self/exe, the kernel's name for the running
 * executable, read with readlink and run with execve (host_linux.c,
 * host_posix.c). On Windows (host_windows.c), which has no exec:
 * GetModuleFileNameW for the path, and for the restart CreateProcessW of the
 * same file as a child, a wait for it, and TerminateProcess with its status
 * (research/m8-inventory.md, 1.2 rows 9, 10 and 20).
 */
#ifndef PLATFORM_HOST_PROCESS_H
#define PLATFORM_HOST_PROCESS_H

#include <stddef.h>

/* The longest path host_process_executable_path writes, its terminating NUL
 * included (Linux's PATH_MAX). */
#define HOST_PROCESS_PATH_SIZE 4096u

/* Writes the path of the running executable's file, NUL-terminated, to out
 * (size bytes); on Windows in the ANSI code page, as the C runtime's fopen
 * takes a path. Returns 0, or -1 after a report on stderr if it cannot be
 * read, does not fit or (Windows) holds a character the code page lacks. */
int host_process_executable_path(char *out, size_t size);

/* Replaces the process with a new run of its own executable, with argv
 * (NULL-terminated, argv[0] first), the current environment and the same
 * standard input, output and error. Flushes nothing. Returns only on failure:
 * -1 with errno set.
 *
 * Linux: execve, so the same process id and all the open descriptors, and
 * async-signal-safe, so a crash handler may call it. Windows: the new run is
 * a child with its own process id, which inherits the standard handles and
 * no other, in a job that ends it if this process is ended; this process
 * waits for it and ends with its exit status, so whoever waits for this
 * process sees the child's end. CreateProcessW allocates and takes
 * locks; a crash handler calls it all the same, the one exception to the
 * handlers' rules (docs/DECISIONS.md, "A crash on Windows restarts through
 * CreateProcessW in the unhandled-exception filter"). */
int host_process_restart(char *const argv[]);

#endif /* PLATFORM_HOST_PROCESS_H */
