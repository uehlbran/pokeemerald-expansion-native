/*
 * host_linux.c: the host layer where Linux does things its own way: the
 * pages mapped at fixed addresses (host_memory.h), the saved registers of a
 * fault (host_fault.h), the executable's path (host_process.h), the image and
 * stack bounds (host_image.h), the stack switch (host_stack.h), and, for
 * host_posix.c (host_internal.h), the flush of a file to the disk and the
 * monotonic wait of the watchdog thread. What every POSIX system shares is
 * in host_posix.c; where the link put the regions, in host_linked.c.
 *
 * Followed: the Intel SDM vol. 2 (table 2-2) for the ModRM register order,
 * glibc's <sys/ucontext.h> for where the kernel saves each register (REG_*
 * indices into uc_mcontext.gregs, i386 and x86-64), the ELF specification
 * (elf(5)) for the program headers, proc(5) for /proc/self/exe, mmap(2) for
 * MAP_FIXED_NOREPLACE, getcontext(3), makecontext(3) for the switch, fsync(2),
 * and POSIX's pthread_condattr_setclock and pthread_cond_timedwait.
 */
#define _GNU_SOURCE /* REG_EAX, REG_RAX and friends; pthread_getattr_np */

#include <errno.h>
#include <inttypes.h>
#include <link.h> /* ElfW */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include "host_fault.h"
#include "host_image.h"
#include "host_internal.h"
#include "host_memory.h"
#include "host_process.h"
#include "host_stack.h"

/* --- host_fault: which SIGFPE is a divide error ---------------------------------- */

/* The kernel gives a divide error FPE_INTDIV and a SIGFPE that kill or raise
 * sent SI_USER or SI_TKILL. */
bool host_fault_is_divide_error(const struct host_fault *fault)
{
    return fault->signal_number == SIGFPE && fault->info->si_code == FPE_INTDIV;
}

/* --- host_fault: the saved registers ------------------------------------------ */

/* Where the kernel saved each register, as indices into gregs: the general
 * registers by ModRM number (Intel SDM vol. 2, table 2-2, and 2.2.1.2 for R8
 * to R15), and the ones host_fault.h names. */
#if defined(__i386__)

static const int general_slots[] = {REG_EAX, REG_ECX, REG_EDX, REG_EBX, REG_ESP, REG_EBP, REG_ESI, REG_EDI};
static const int named_slots[] = {
    [HOST_FAULT_PC] = REG_EIP, [HOST_FAULT_ACCUMULATOR] = REG_EAX, [HOST_FAULT_DATA] = REG_EDX};

#elif defined(__x86_64__)

static const int general_slots[] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
                                    REG_R8,  REG_R9,  REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15};
static const int named_slots[] = {
    [HOST_FAULT_PC] = REG_RIP, [HOST_FAULT_ACCUMULATOR] = REG_RAX, [HOST_FAULT_DATA] = REG_RDX};

#else
#error "host_linux.c knows where the kernel saves the registers of i386 and x86-64 only"
#endif

#define GENERAL_SLOT_COUNT (sizeof general_slots / sizeof general_slots[0])
#define NAMED_SLOT_COUNT (sizeof named_slots / sizeof named_slots[0])

/* The registers are pointer-sized integers in gregs at both widths. */
uintptr_t host_fault_register(const struct host_fault *fault, enum host_fault_register name)
{
    if ((size_t)name >= NAMED_SLOT_COUNT)
        return 0;
    return (uintptr_t)fault->context->uc_mcontext.gregs[named_slots[name]];
}

void host_fault_set_register(struct host_fault *fault, enum host_fault_register name, uintptr_t value)
{
    if ((size_t)name >= NAMED_SLOT_COUNT)
        return;
    fault->context->uc_mcontext.gregs[named_slots[name]] = (greg_t)value;
}

uintptr_t host_fault_general_register(const struct host_fault *fault, unsigned number)
{
    return (uintptr_t)fault->context->uc_mcontext.gregs[general_slots[number % GENERAL_SLOT_COUNT]];
}

/* --- host_process: the executable's path ------------------------------------------ */

/* proc(5): a symbolic link to the running executable, which execve runs
 * even if its path has since been replaced. */
static const char self_executable[] = "/proc/self/exe";

const char *host_process_exec_path(void)
{
    return self_executable;
}

int host_process_executable_path(char *out, size_t size)
{
    const ssize_t length = readlink(self_executable, out, size);
    if (length < 0) {
        (void)fprintf(stderr, "host_process: readlink(%s): %s\n", self_executable, strerror(errno));
        return -1;
    }
    if ((size_t)length >= size) { /* readlink truncates silently */
        (void)fprintf(stderr, "host_process: the executable's path is %zd bytes or longer, more than %zu\n", length,
                      size - 1u);
        return -1;
    }
    out[length] = '\0';
    return 0;
}

/* --- host_memory: pages mapped at run time ---------------------------------------- */

int host_memory_map_fixed(uintptr_t address, size_t size)
{
    void *const wanted = (void *)address;
    void *const got = mmap(wanted, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1,
                           0);
    if (got == MAP_FAILED) {
        (void)fprintf(stderr, "host_memory: mmap at 0x%08" PRIXPTR " failed: %s\n", address, strerror(errno));
        return -1;
    }
    if (got != wanted) { /* a kernel before 4.17 takes MAP_FIXED_NOREPLACE for a hint */
        (void)fprintf(stderr, "host_memory: mmap at 0x%08" PRIXPTR " returned %p\n", address, got);
        if (munmap(got, size) != 0)
            (void)fprintf(stderr, "host_memory: munmap failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int host_memory_unmap_fixed(uintptr_t address, size_t size)
{
    if (munmap((void *)address, size) != 0) {
        (void)fprintf(stderr, "host_memory: munmap at 0x%08" PRIXPTR " failed: %s\n", address, strerror(errno));
        return -1;
    }
    return 0;
}

/* --- host_image ------------------------------------------------------------------------ */

extern const unsigned char __executable_start[], __data_start[], _end[]; /* GNU ld / glibc crt1 */

struct host_span host_image_whole(void)
{
    return host_span_of(__executable_start, _end);
}

struct host_span host_image_writable(void)
{
    return host_span_of(__data_start, _end);
}

/* The read-only PT_LOAD segments, from the program headers the kernel passed
 * (getauxval(AT_PHDR)): code, read-only data and the headers themselves. */
bool host_image_is_read_only(uintptr_t address)
{
    const ElfW(Phdr) *headers = (const ElfW(Phdr) *)(uintptr_t)getauxval(AT_PHDR);
    const size_t count = (size_t)getauxval(AT_PHNUM);
    if (headers == NULL) /* getauxval gives 0 when the kernel passed no AT_PHDR: then nothing reads as ROM */
        return false;
    /* The executable is non-PIE (load bias 0); PT_PHDR gives the bias anyway. */
    uintptr_t bias = 0;
    for (size_t i = 0; i < count; i++) {
        if (headers[i].p_type == PT_PHDR)
            bias = (uintptr_t)headers - (uintptr_t)headers[i].p_vaddr;
    }
    for (size_t i = 0; i < count; i++) {
        const ElfW(Phdr) *header = &headers[i];
        if (header->p_type != PT_LOAD || (header->p_flags & PF_W) != 0)
            continue;
        const uintptr_t start = bias + (uintptr_t)header->p_vaddr;
        if (address >= start && address - start < (uintptr_t)header->p_memsz)
            return true;
    }
    return false;
}

/* The whole mapping glibc reports through pthread_getattr_np, chosen over
 * parsing /proc/self/maps because it needs no text parsing. On the main
 * thread glibc derives the bounds from the stack's resource limit. */
int host_image_main_stack(struct host_span *out)
{
    pthread_attr_t attributes;
    void *low = NULL;
    size_t size = 0;
    int error = pthread_getattr_np(pthread_self(), &attributes);
    if (error != 0) {
        (void)fprintf(stderr, "host_image: pthread_getattr_np failed: %s\n", strerror(error));
        return -1;
    }
    error = pthread_attr_getstack(&attributes, &low, &size);
    if (error != 0)
        (void)fprintf(stderr, "host_image: pthread_attr_getstack failed: %s\n", strerror(error));
    const int destroy_error = pthread_attr_destroy(&attributes);
    if (destroy_error != 0)
        (void)fprintf(stderr, "host_image: pthread_attr_destroy failed: %s\n", strerror(destroy_error));
    if (error != 0 || destroy_error != 0)
        return -1;
    const uintptr_t start = (uintptr_t)low;
    if (size == 0 || size > UINTPTR_MAX - start) {
        (void)fprintf(stderr, "host_image: pthread_attr_getstack returned an unusable stack\n");
        return -1;
    }
    *out = (struct host_span){.start = start, .end = start + size};
    return 0;
}

/* --- host_stack -------------------------------------------------------------------------- */

/* The one switch host_stack_run has in progress. */
static struct {
    ucontext_t caller;
    ucontext_t callee;
    void (*entry)(void *context);
    void *context;
    /* AddressSanitizer's view of the caller's stack, for switching back. */
    void *caller_fake_stack;
    const void *caller_stack_bottom;
    size_t caller_stack_size;
} switched;

/* The first function on the new stack: makecontext passes no pointer
 * arguments portably, so it reads the entry from `switched`. Returning
 * resumes the caller through uc_link. */
static void run_entry(void)
{
    host_stack_finish_switch(NULL, &switched.caller_stack_bottom, &switched.caller_stack_size);
    switched.entry(switched.context);
    host_stack_start_switch(NULL, switched.caller_stack_bottom, switched.caller_stack_size);
}

int host_stack_run(void *stack, size_t size, void (*entry)(void *context), void *context)
{
    if (getcontext(&switched.callee) != 0) {
        perror("host_stack: getcontext");
        return -1;
    }
    switched.callee.uc_stack.ss_sp = stack;
    switched.callee.uc_stack.ss_size = size;
    switched.callee.uc_link = &switched.caller;
    makecontext(&switched.callee, run_entry, 0);
    switched.entry = entry;
    switched.context = context;

    host_stack_start_switch(&switched.caller_fake_stack, stack, size);
    const int swapped = swapcontext(&switched.caller, &switched.callee);
    host_stack_finish_switch(switched.caller_fake_stack, NULL, NULL);
    if (swapped != 0) {
        perror("host_stack: swapcontext");
        return -1;
    }
    return 0;
}

void host_stack_leave(void)
{
    host_stack_start_switch(NULL, switched.caller_stack_bottom, switched.caller_stack_size);
    (void)setcontext(&switched.caller); /* returns only on failure */
    (void)fprintf(stderr, "host_stack: setcontext back to the caller's stack failed\n");
}

/* --- host_file: the flush ------------------------------------------------------ */

/* fsync(2): on Linux it returns once the drive reports the data stored,
 * flushing a volatile write cache on the way. */
int host_file_flush(int descriptor)
{
    return fsync(descriptor);
}

/* --- host_thread: the monotonic wait --------------------------------------------- */

int host_condition_init(pthread_cond_t *condition)
{
    pthread_condattr_t attributes;
    int error = pthread_condattr_init(&attributes);
    if (error != 0)
        return error;
    error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (error == 0)
        error = pthread_cond_init(condition, &attributes);
    (void)pthread_condattr_destroy(&attributes); /* cannot fail on POSIX systems; the init has copied what it needs */
    return error;
}

/* The condition variable measures CLOCK_MONOTONIC (host_condition_init), so
 * the deadline is the absolute time pthread_cond_timedwait takes. */
int host_condition_wait_until(pthread_cond_t *condition, pthread_mutex_t *lock, const struct timespec *deadline)
{
    return pthread_cond_timedwait(condition, lock, deadline);
}
