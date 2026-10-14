/*
 * host_internal.h: what host_posix.c, the host layer's source for every
 * POSIX system, takes from the source of the system it runs on
 * (host_linux.c). Not an interface: modules include the host_*.h headers.
 */
#ifndef PLATFORM_HOST_INTERNAL_H
#define PLATFORM_HOST_INTERNAL_H

#include <signal.h>
#include <ucontext.h>

/* host_fault.h's fault: the signal, what the kernel says about it, and the
 * interrupted context, whose saved registers the handler may edit. */
struct host_fault {
    int signal_number;
    siginfo_t *info;
    ucontext_t *context;
};

/* The path execve runs this executable by: a constant, so that a signal
 * handler may use it (Linux: /proc/self/exe). */
extern const char host_self_executable[];

#endif /* PLATFORM_HOST_INTERNAL_H */
