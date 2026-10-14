/*
 * host_fault.h: hardware faults, handled with the context they interrupted.
 *
 * Stands in for nothing on the GBA, whose CPU has no divide instruction and
 * reboots through the BIOS's reset vector after a crash; this is how the
 * host hands the platform the faults that model those two behaviours:
 * arm_division completes an x86 divide error as the GBA's libgcc would, and
 * crash_reboot reboots after a crash (both headers say how).
 *
 * Wraps, on Linux: signals (sigaction with SA_SIGINFO and SA_ONSTACK,
 * sigaltstack, sigprocmask, raise; host_posix.c) and the registers the
 * kernel saved in the handler's ucontext_t (uc_mcontext.gregs, i386 and
 * x86-64; host_linux.c). On Windows (host_windows_fault.c): a vectored
 * exception handler, called first, for the divide error; the process's
 * unhandled-exception filter for the crash handler; the registers the system
 * saved in the exception's CONTEXT (x86-64). The handlers run in a signal
 * handler or an exception handler: they may call only async-signal-safe
 * functions (signal-safety(7)), which the accessors below, host_file's
 * host_file_write_all, host_process_end and, on Linux, host_process_restart
 * are (on Windows the restart creates a process, which a handler must not:
 * host_process_restart_is_handler_safe). They are installed and
 * removed from the main thread; a fault of another thread (SDL's) reaches
 * them too, on the thread that faulted.
 */
#ifndef PLATFORM_HOST_FAULT_H
#define PLATFORM_HOST_FAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A fault being handled: what happened and the interrupted context. Valid
 * only inside the handler it is passed to. */
struct host_fault;

/* The registers a handler reads or writes by name. */
enum host_fault_register {
    HOST_FAULT_PC,          /* EIP or RIP: the faulting instruction */
    HOST_FAULT_ACCUMULATOR, /* EAX or RAX */
    HOST_FAULT_DATA         /* EDX or RDX */
};

/* The bytes of the alternate stack the crash handler runs on (a crash may be
 * a stack overflow), which a divide-error handler then runs on too. Far
 * above glibc's MINSIGSTKSZ at both widths, with room for the sanitizers'
 * interceptors. Windows has no alternate stack: this many bytes at the end of
 * the main thread's stack are kept for the handler of a stack overflow
 * (SetThreadStackGuarantee). */
#define HOST_FAULT_ALTERNATE_STACK_SIZE (64u * 1024u)

/* A divide-error handler: true if it completed the faulting instruction in
 * the context (execution resumes where the context's program counter now
 * points), false to pass the fault on. */
typedef bool (*host_fault_divide_handler)(struct host_fault *fault);

/* A crash handler: reports, and may start the program again
 * (host_process_restart, where host_process_restart_is_handler_safe) or end
 * the process with a status of its own (host_process_end). When it returns,
 * the fault ends the process as it would have without a handler (Windows:
 * with the exception's code as the exit status). */
typedef void (*host_fault_crash_handler)(const struct host_fault *fault);

/* Installs handler for the integer divide error (x86 #DE: a division by zero
 * or a quotient too wide; Linux: every SIGFPE, so the handler tells a divide
 * error from another one with host_fault_is_divide_error; Windows:
 * EXCEPTION_INT_DIVIDE_BY_ZERO and EXCEPTION_INT_OVERFLOW only), keeping the
 * handler it replaces. A fault the handler passes on goes to that handler
 * (Windows: on through the system's dispatch, to the crash handler if one is
 * installed), or, if there was none, to the default action, so the faulting
 * instruction ends the process. Windows: also turns the system's error
 * dialogs off, so that a crash cannot wait for a click. Calling it again
 * first uninstalls. Returns 0, or -1 after a report on stderr with nothing
 * installed. */
int host_fault_install_divide_error(host_fault_divide_handler handler);

/* Restores the divide-error handler that was installed before. Safe at any
 * time, and twice. */
void host_fault_uninstall_divide_error(void);

/* Installs handler for every fault that crashes the program (Linux: SIGSEGV,
 * SIGBUS, SIGILL, SIGFPE, SIGABRT; Windows: any exception no handler took,
 * through the unhandled-exception filter, but not abort(), which is no
 * exception there), on an alternate stack of HOST_FAULT_ALTERNATE_STACK_SIZE
 * bytes (Windows: that much stack kept for it), and unblocks those faults (a
 * handler's mask survives execve, so a restarted process starts with its
 * fault blocked; Windows blocks none, and turns its error dialogs off).
 * Calling it again first uninstalls. Returns 0, or -1 after a report on
 * stderr with nothing installed. */
int host_fault_install_crash(host_fault_crash_handler handler);

/* Restores the default actions of the crash faults and removes the alternate
 * stack (Windows: puts the filter it replaced back; the kept stack stays).
 * Safe at any time, and twice. */
void host_fault_uninstall_crash(void);

/* True if the fault is an integer divide error raised by an instruction (not
 * a signal another process or raise() sent; Windows cannot tell a
 * RaiseException with a divide error's code, which the decoder then refuses). */
bool host_fault_is_divide_error(const struct host_fault *fault);

/* The host's number for the fault, for reports: the signal number; Windows:
 * the exception code (an NTSTATUS, negative as an int). */
int host_fault_number(const struct host_fault *fault);

/* Room for host_fault_name's text, its NUL included. */
#define HOST_FAULT_NAME_SIZE 24u

/* The fault's name for a report, as the host's own tools write it: "signal 11"
 * (the signal number in decimal; POSIX), "exception 0xC0000005" (the exception
 * code in hexadecimal, as Microsoft's documentation and debuggers write an
 * NTSTATUS; Windows). Writes it to name, NUL-terminated. Async-signal-safe. */
void host_fault_name(const struct host_fault *fault, char name[HOST_FAULT_NAME_SIZE]);

/* The address the fault concerns, for reports: the faulting memory access,
 * or for some faults the instruction (siginfo's si_addr; Windows: an access
 * violation's accessed address, else the exception's address). */
uintptr_t host_fault_address(const struct host_fault *fault);

/* The interrupted value of register `name`. */
uintptr_t host_fault_register(const struct host_fault *fault, enum host_fault_register name);

/* Sets register `name` to value in the context the fault resumes with. */
void host_fault_set_register(struct host_fault *fault, enum host_fault_register name, uintptr_t value);

/* The interrupted value of the general register with ModRM number `number`
 * (Intel SDM vol. 2, table 2-2: 0 EAX ... 7 EDI; on x86-64 RAX ... RDI and,
 * with REX, 8 to 15 for R8 to R15), taken modulo the register count. */
uintptr_t host_fault_general_register(const struct host_fault *fault, unsigned number);

#endif /* PLATFORM_HOST_FAULT_H */
