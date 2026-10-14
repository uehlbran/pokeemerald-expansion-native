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
 * host_posix.c). Another OS adds its own source behind the same calls
 * (research/m8-inventory.md, 1.2 rows 9, 10 and 20).
 */
#ifndef PLATFORM_HOST_PROCESS_H
#define PLATFORM_HOST_PROCESS_H

#include <stddef.h>

/* The longest path host_process_executable_path writes, its terminating NUL
 * included (Linux's PATH_MAX). */
#define HOST_PROCESS_PATH_SIZE 4096u

/* Writes the path of the running executable's file, NUL-terminated, to out
 * (size bytes). Returns 0, or -1 after a report on stderr if it cannot be
 * read or does not fit. */
int host_process_executable_path(char *out, size_t size);

/* Replaces the process with a new run of its own executable, with argv
 * (NULL-terminated, argv[0] first) and the current environment: the same
 * process id and the same open descriptors. Async-signal-safe, so a crash
 * handler may call it. Flushes nothing. Returns only on failure: -1 with
 * errno set. */
int host_process_restart(char *const argv[]);

#endif /* PLATFORM_HOST_PROCESS_H */
