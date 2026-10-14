/*
 * host_windows_fault.c: hardware faults on Windows (host_fault.h): the divide
 * error through a vectored exception handler, the crash handler as the
 * process's unhandled-exception filter, and the registers the system saved
 * in the fault's CONTEXT.
 *
 * Followed: Microsoft's documentation of AddVectoredExceptionHandler,
 * SetUnhandledExceptionFilter, SetErrorMode, SetThreadStackGuarantee,
 * EXCEPTION_RECORD and the x64 CONTEXT (winnt.h), and the Intel SDM vol. 2
 * (table 2-2, and 2.2.1.2 for R8 to R15) for the ModRM register order, the
 * same as host_linux.c's.
 *
 * Where each fault goes. x86's divide error (#DE) is
 * EXCEPTION_INT_DIVIDE_BY_ZERO for a zero divisor and EXCEPTION_INT_OVERFLOW
 * for a quotient too wide (INT_MIN / -1); the vectored handler, called first,
 * before any frame's handler, gives those two and nothing else to the divide
 * handler. One it passes on, like every other exception no frame takes,
 * reaches the unhandled-exception filter: the crash handler, then the end of
 * the process. A crash handler that returns lets it end with the exception's
 * code, as without one (the filter ends it, on_unhandled_exception says why).
 *
 * What a handler may do (docs/C-RULES.md, "Concurrency"). It runs on the
 * thread that faulted, the game's or, rarely, one of SDL's, so the handler
 * pointers it reads are C11 atomics: stored with release by install, loaded
 * with acquire by the handler, so that a handler that sees a pointer also
 * sees what was set up before it was stored; uninstall clears them (relaxed:
 * the removal of the handler itself orders the rest). Everything on the
 * handlers' own path reads the fault's records or writes the saved context;
 * the crash handler's restart is host_process_restart's (host_process.h).
 *
 * What has no counterpart. Windows blocks no faults, so there is nothing to
 * unblock. There is no alternate stack: a handler runs on the faulting
 * thread's own stack. For a stack overflow (EXCEPTION_STACK_OVERFLOW, raised
 * on the main stack's last guard page) SetThreadStackGuarantee keeps
 * HOST_FAULT_ALTERNATE_STACK_SIZE bytes at the end of the installing thread's
 * stack for the handler, which the crash reboot's restart needs. An overflow
 * of the battle runner's stack (game_hooks) runs into the image's read-only
 * headers below it, where the system cannot write the exception's records:
 * the process ends without the handler.
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>

#include "host_fault.h"

#if !defined(__x86_64__)
#error "host_windows_fault.c knows the CONTEXT of x86-64 only (the Windows build is 64-bit)"
#endif

/* host_fault.h's fault: what the system says about the exception, and the
 * context it interrupted, whose saved registers the handler may edit. */
struct host_fault {
    EXCEPTION_POINTERS *exception;
};

/* AddVectoredExceptionHandler: a non-zero first argument calls the handler
 * before those already added. */
#define CALL_FIRST 1u

/* A crash must not wait for someone to close a dialog, which a CI runner
 * cannot: no Windows Error Reporting box for an unhandled fault, no
 * critical-error box for a drive that fails. Process-wide, and kept after
 * uninstall. */
static void keep_error_dialogs_off(void)
{
    (void)SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

/* --- host_fault: the divide error ---------------------------------------------------- */

static struct {
    _Atomic(host_fault_divide_handler) handler;
    PVOID registration; /* AddVectoredExceptionHandler's; install's and uninstall's only */
} divide;

static bool is_divide_error_code(DWORD code)
{
    return code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW;
}

/* Every exception of every thread comes here first, many of them handled
 * later by a frame of their own (a DLL's probe, a debugger's message): all
 * but the divide error go on untouched. */
static LONG WINAPI on_vectored_exception(EXCEPTION_POINTERS *exception)
{
    if (!is_divide_error_code(exception->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;
    const host_fault_divide_handler handler = atomic_load_explicit(&divide.handler, memory_order_acquire);
    if (handler == NULL)
        return EXCEPTION_CONTINUE_SEARCH;
    struct host_fault fault = {.exception = exception};
    return handler(&fault) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

int host_fault_install_divide_error(host_fault_divide_handler handler)
{
    host_fault_uninstall_divide_error();
    keep_error_dialogs_off();
    atomic_store_explicit(&divide.handler, handler, memory_order_release);
    divide.registration = AddVectoredExceptionHandler(CALL_FIRST, on_vectored_exception);
    if (divide.registration == NULL) {
        atomic_store_explicit(&divide.handler, NULL, memory_order_relaxed);
        (void)fprintf(stderr, "host_fault: AddVectoredExceptionHandler failed\n");
        return -1;
    }
    return 0;
}

void host_fault_uninstall_divide_error(void)
{
    if (divide.registration == NULL)
        return;
    (void)RemoveVectoredExceptionHandler(divide.registration); /* fails only for a handle it never gave */
    divide.registration = NULL;
    atomic_store_explicit(&divide.handler, NULL, memory_order_relaxed);
}

/* --- host_fault: crashes --------------------------------------------------------------- */

static struct {
    _Atomic(host_fault_crash_handler) handler;
    bool installed;
    LPTOP_LEVEL_EXCEPTION_FILTER previous; /* the C runtime's, put back by uninstall */
} crash;

/* The filter the system calls for an exception no frame took, after the
 * vectored handlers. When the handler returns, the filter ends the process
 * itself, with the exception's code as its exit status, which is the status
 * the system's default would give. It does not hand the exception back to that
 * default: the default unwinds to the thread's first frame, and the unwind
 * refuses a frame above its target, which every frame on the battle runner's
 * stack is when the main stack lies below it (host_windows_stack.c), so a
 * fault there would end with another code or come back here. */
static LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS *exception)
{
    const host_fault_crash_handler handler = atomic_load_explicit(&crash.handler, memory_order_acquire);
    if (handler == NULL)
        return EXCEPTION_CONTINUE_SEARCH;
    const struct host_fault fault = {.exception = exception};
    handler(&fault);
    (void)TerminateProcess(GetCurrentProcess(), exception->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH; /* not reached: TerminateProcess ends this thread with its process */
}

int host_fault_install_crash(host_fault_crash_handler handler)
{
    host_fault_uninstall_crash();
    keep_error_dialogs_off();
    ULONG guarantee = HOST_FAULT_ALTERNATE_STACK_SIZE;
    if (!SetThreadStackGuarantee(&guarantee)) {
        (void)fprintf(stderr, "host_fault: SetThreadStackGuarantee(%u) failed (Windows error %lu)\n",
                      HOST_FAULT_ALTERNATE_STACK_SIZE, (unsigned long)GetLastError());
        return -1;
    }
    atomic_store_explicit(&crash.handler, handler, memory_order_release);
    crash.previous = SetUnhandledExceptionFilter(on_unhandled_exception);
    crash.installed = true;
    return 0;
}

/* The stack guarantee stays: Windows can raise it, not lower it. */
void host_fault_uninstall_crash(void)
{
    if (!crash.installed)
        return;
    (void)SetUnhandledExceptionFilter(crash.previous);
    atomic_store_explicit(&crash.handler, NULL, memory_order_relaxed);
    crash.installed = false;
}

/* --- host_fault: what a handler reads ---------------------------------------------------- */

/* A RaiseException with one of these codes would pass too; arm_division then
 * finds no division at the program counter and passes it on. */
bool host_fault_is_divide_error(const struct host_fault *fault)
{
    return is_divide_error_code(fault->exception->ExceptionRecord->ExceptionCode);
}

/* The exception code, an NTSTATUS (0xC0000005 for an access violation): as
 * an int it is negative, and crash_reboot prints it unsigned, in decimal. */
int host_fault_number(const struct host_fault *fault)
{
    return (int)fault->exception->ExceptionRecord->ExceptionCode;
}

/* An access violation's and an in-page error's second parameter is the
 * address accessed; for any other fault, the instruction's. */
#define ACCESSED_ADDRESS_PARAMETER 1u

uintptr_t host_fault_address(const struct host_fault *fault)
{
    const EXCEPTION_RECORD *record = fault->exception->ExceptionRecord;
    const bool names_access = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                              || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR;
    if (names_access && record->NumberParameters > ACCESSED_ADDRESS_PARAMETER)
        return (uintptr_t)record->ExceptionInformation[ACCESSED_ADDRESS_PARAMETER];
    return (uintptr_t)record->ExceptionAddress;
}

/* --- host_fault: the saved registers ------------------------------------------------------ */

_Static_assert(sizeof(DWORD64) == sizeof(uintptr_t), "a CONTEXT register is a pointer-sized integer on x86-64");

/* Where the system saved each register in the CONTEXT: the general registers
 * by ModRM number, and the ones host_fault.h names. */
static const size_t general_slots[] = {
    offsetof(CONTEXT, Rax), offsetof(CONTEXT, Rcx), offsetof(CONTEXT, Rdx), offsetof(CONTEXT, Rbx),
    offsetof(CONTEXT, Rsp), offsetof(CONTEXT, Rbp), offsetof(CONTEXT, Rsi), offsetof(CONTEXT, Rdi),
    offsetof(CONTEXT, R8),  offsetof(CONTEXT, R9),  offsetof(CONTEXT, R10), offsetof(CONTEXT, R11),
    offsetof(CONTEXT, R12), offsetof(CONTEXT, R13), offsetof(CONTEXT, R14), offsetof(CONTEXT, R15)};
static const size_t named_slots[] = {[HOST_FAULT_PC] = offsetof(CONTEXT, Rip),
                                     [HOST_FAULT_ACCUMULATOR] = offsetof(CONTEXT, Rax),
                                     [HOST_FAULT_DATA] = offsetof(CONTEXT, Rdx)};

#define GENERAL_SLOT_COUNT (sizeof general_slots / sizeof general_slots[0])
#define NAMED_SLOT_COUNT (sizeof named_slots / sizeof named_slots[0])

static uintptr_t read_slot(const CONTEXT *context, size_t offset)
{
    DWORD64 value = 0;
    memcpy(&value, (const unsigned char *)context + offset, sizeof value);
    return (uintptr_t)value;
}

uintptr_t host_fault_register(const struct host_fault *fault, enum host_fault_register name)
{
    if ((size_t)name >= NAMED_SLOT_COUNT)
        return 0;
    return read_slot(fault->exception->ContextRecord, named_slots[name]);
}

/* The context resumes with what is written here when the vectored handler
 * returns EXCEPTION_CONTINUE_EXECUTION (its ContextFlags include the integer
 * and control registers for a hardware exception). */
void host_fault_set_register(struct host_fault *fault, enum host_fault_register name, uintptr_t value)
{
    if ((size_t)name >= NAMED_SLOT_COUNT)
        return;
    const DWORD64 saved = value;
    memcpy((unsigned char *)fault->exception->ContextRecord + named_slots[name], &saved, sizeof saved);
}

uintptr_t host_fault_general_register(const struct host_fault *fault, unsigned number)
{
    return read_slot(fault->exception->ContextRecord, general_slots[number % GENERAL_SLOT_COUNT]);
}
