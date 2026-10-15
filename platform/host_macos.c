/*
 * host_macos.c: the host layer where macOS does things its own way: the
 * saved registers of a fault (host_fault.h), the executable's path
 * (host_process.h), pages mapped at a fixed address (host_memory.h), the
 * image and stack bounds (host_image.h), and, for host_posix.c
 * (host_internal.h), the flush of a file to the disk and the watchdog's
 * monotonic wait. What every POSIX system shares is in host_posix.c; the
 * stack switch is host_macos_stack.c; where the link put the regions,
 * host_linked.c, whose names the link flags bind to ld64's section bounds
 * (platform/target.mk).
 *
 * x86_64 only: the memory model needs a fixed-address image, which arm64
 * macOS never loads (docs/DECISIONS.md, "macOS ships as x86_64 under
 * Rosetta"); an Apple-silicon Mac runs this build through Rosetta 2.
 *
 * Followed: <mach/i386/_structs.h> for where the kernel saves each register
 * (uc_mcontext->__ss, named fields where Linux has gregs indices), the Intel
 * SDM vol. 2 (table 2-2) for the ModRM register order, dyld(3) for
 * _NSGetExecutablePath, <mach-o/loader.h> and <mach-o/ldsyms.h> for the
 * image's load commands, ld64's segment$start$ and segment$end$ symbols,
 * mmap(2), pthread_get_stackaddr_np(3), fcntl(2)'s F_FULLFSYNC and fsync(2),
 * and pthread_cond_timedwait_relative_np in <pthread.h>.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mach-o/dyld.h>
#include <mach-o/ldsyms.h>
#include <mach-o/loader.h>
#include <mach/vm_prot.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "host_fault.h"
#include "host_image.h"
#include "host_internal.h"
#include "host_memory.h"
#include "host_process.h"
#include "x86_divide.h"

#if !defined(__x86_64__)
#error "host_macos.c is for x86_64 macOS: arm64 cannot load the fixed-address image (docs/DECISIONS.md)"
#endif

/* --- host_fault: the saved registers ------------------------------------------ */

/* The thread state the kernel saved for the handler, _STRUCT_X86_THREAD_STATE64. */
typedef struct __darwin_x86_thread_state64 saved_registers;

/* Where each register is in it: the general registers by ModRM number (Intel
 * SDM vol. 2, table 2-2, and 2.2.1.2 for R8 to R15), and the ones
 * host_fault.h names. Offsets, since the registers are named fields. */
static const size_t general_slots[] = {
    offsetof(saved_registers, __rax), offsetof(saved_registers, __rcx), offsetof(saved_registers, __rdx),
    offsetof(saved_registers, __rbx), offsetof(saved_registers, __rsp), offsetof(saved_registers, __rbp),
    offsetof(saved_registers, __rsi), offsetof(saved_registers, __rdi), offsetof(saved_registers, __r8),
    offsetof(saved_registers, __r9),  offsetof(saved_registers, __r10), offsetof(saved_registers, __r11),
    offsetof(saved_registers, __r12), offsetof(saved_registers, __r13), offsetof(saved_registers, __r14),
    offsetof(saved_registers, __r15)};
static const size_t named_slots[] = {[HOST_FAULT_PC] = offsetof(saved_registers, __rip),
                                     [HOST_FAULT_ACCUMULATOR] = offsetof(saved_registers, __rax),
                                     [HOST_FAULT_DATA] = offsetof(saved_registers, __rdx)};

#define GENERAL_SLOT_COUNT (sizeof general_slots / sizeof general_slots[0])
#define NAMED_SLOT_COUNT (sizeof named_slots / sizeof named_slots[0])

/* Every register in the state is a 64-bit field; memcpy, not a cast to a
 * pointer of another type. */
static uintptr_t read_slot(const struct host_fault *fault, size_t offset)
{
    uint64_t value = 0;
    memcpy(&value, (const unsigned char *)&fault->context->uc_mcontext->__ss + offset, sizeof value);
    return (uintptr_t)value;
}

uintptr_t host_fault_register(const struct host_fault *fault, enum host_fault_register name)
{
    if ((size_t)name >= NAMED_SLOT_COUNT)
        return 0;
    return read_slot(fault, named_slots[name]);
}

void host_fault_set_register(struct host_fault *fault, enum host_fault_register name, uintptr_t value)
{
    if ((size_t)name >= NAMED_SLOT_COUNT)
        return;
    const uint64_t wide = value;
    memcpy((unsigned char *)&fault->context->uc_mcontext->__ss + named_slots[name], &wide, sizeof wide);
}

uintptr_t host_fault_general_register(const struct host_fault *fault, unsigned number)
{
    return read_slot(fault, general_slots[number % GENERAL_SLOT_COUNT]);
}

/* --- host_fault: which SIGFPE is a divide error ------------------------------------ */

/* macOS gives a SIGFPE that kill or raise sends FPE_INTDIV too, as it gives a
 * divide error (measured on macos-15-intel; docs/DECISIONS.md, "On macOS a
 * SIGFPE sent by kill looks like a divide error"), so si_code alone cannot
 * tell them apart. A divide error is also a DIV or IDIV at the program
 * counter, where the processor stopped (x86_divide.h). A SIGFPE sent while the
 * program counter happens to be at such an instruction still reads as a
 * divide error (docs/DECISIONS.md, "On macOS a SIGFPE sent by kill looks like
 * a divide error"). The bytes read are at the saved program counter, in code
 * that is mapped: the instruction that faulted, or raise's own. */
bool host_fault_is_divide_error(const struct host_fault *fault)
{
    if (fault->signal_number != SIGFPE || fault->info->si_code != FPE_INTDIV)
        return false;
    return x86_divide_at((const unsigned char *)host_fault_register(fault, HOST_FAULT_PC));
}

/* --- host_process: the executable's path ------------------------------------------ */

/* The path dyld recorded when the program started, absolute even when it was
 * started by a relative one (measured on macos-15-intel).
 * macOS has no /proc/self/exe, and _NSGetExecutablePath is not on
 * signal-safety(7)'s list, so it is read once, by a constructor, before main
 * and before any handler exists; the crash handler's execve then reads this
 * buffer only. A path that does not fit leaves it empty, and needed_size says
 * how long it was. */
static struct {
    char path[HOST_PROCESS_PATH_SIZE];
    uint32_t needed_size;
} executable;

__attribute__((constructor)) static void read_executable_path(void)
{
    uint32_t size = (uint32_t)sizeof executable.path;
    if (_NSGetExecutablePath(executable.path, &size) != 0) {
        executable.path[0] = '\0';
        executable.needed_size = size;
    }
}

const char *host_process_exec_path(void)
{
    return executable.path;
}

int host_process_executable_path(char *out, size_t size)
{
    if (executable.path[0] == '\0') {
        (void)fprintf(stderr, "host_process: _NSGetExecutablePath needed %" PRIu32 " bytes, more than %u\n",
                      executable.needed_size, HOST_PROCESS_PATH_SIZE);
        return -1;
    }
    const size_t length = strlen(executable.path);
    if (length >= size) {
        (void)fprintf(stderr, "host_process: the executable's path is %zu bytes, more than %zu\n", length, size - 1u);
        return -1;
    }
    memcpy(out, executable.path, length + 1u);
    return 0;
}

/* --- host_memory: pages mapped at run time ---------------------------------------- */

/* macOS has no MAP_FIXED_NOREPLACE, and its MAP_FIXED replaces what is there.
 * Without MAP_FIXED the address is a hint the kernel takes when the range is
 * free and moves away from when it is not, so a mapping anywhere else means
 * something was there: it is undone and refused. */
int host_memory_map_fixed(uintptr_t address, size_t size)
{
    void *const wanted = (void *)address;
    void *const got = mmap(wanted, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (got == MAP_FAILED) {
        (void)fprintf(stderr, "host_memory: mmap at 0x%08" PRIXPTR " failed: %s\n", address, strerror(errno));
        return -1;
    }
    if (got != wanted) {
        (void)fprintf(stderr, "host_memory: 0x%08" PRIXPTR " is taken (mmap gave %p)\n", address, got);
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

/* ld64's bounds of the image's writable data: __DATA's own initialised data
 * (the program's .data: __data, then the game's sections) to the end of the
 * segment, its zero-filled sections included. The pointers dyld fills and
 * __DATA,__const lie before __data, as Linux's .got lies before __data_start. */
extern const unsigned char data_section_start[] __asm("section$start$__DATA$__data");
extern const unsigned char data_segment_end[] __asm("segment$end$__DATA");

struct host_span host_image_whole(void)
{
    return host_span_of((const unsigned char *)&_mh_execute_header, data_segment_end);
}

struct host_span host_image_writable(void)
{
    return host_span_of(data_section_start, data_segment_end);
}

/* The section name of the constant data a writable segment holds. */
#define CONSTANT_SECTION "__const"

/* True if address is in a section named __const among segment's sections
 * (which follow the segment command, at cursor). */
static bool in_constant_section(const unsigned char *cursor, const struct segment_command_64 *segment, uintptr_t slide,
                                uintptr_t address)
{
    for (uint32_t i = 0; i < segment->nsects; i++) {
        struct section_64 section;
        memcpy(&section, cursor + sizeof *segment + i * sizeof section, sizeof section);
        const uintptr_t start = slide + (uintptr_t)section.addr;
        if (strncmp(section.sectname, CONSTANT_SECTION, sizeof section.sectname) == 0 && address >= start
            && address - start < (uintptr_t)section.size)
            return true;
    }
    return false;
}

/* The image's segments, from its own load commands: a segment mapped without
 * write permission (code, read-only data, string literals), or the constant
 * data of a writable one. Mach-O keeps a constant that holds an address
 * (__DATA,__const), and the converted data's read-only data, in the writable
 * __DATA segment, where the loader could fix its pointers; Linux's non-PIE
 * link puts the same data in a read-only segment. __PAGEZERO, mapped with no
 * permission at all, and __LINKEDIT, the symbol tables, are not the
 * program's. */
bool host_image_is_read_only(uintptr_t address)
{
    const struct mach_header_64 *const header = &_mh_execute_header;
    const unsigned char *cursor = (const unsigned char *)(header + 1);
    uintptr_t slide = 0;
    for (uint32_t i = 0; i < header->ncmds; i++) {
        struct load_command command;
        memcpy(&command, cursor, sizeof command);
        if (command.cmd == LC_SEGMENT_64) {
            struct segment_command_64 segment;
            memcpy(&segment, cursor, sizeof segment);
            if (strncmp(segment.segname, SEG_TEXT, sizeof segment.segname) == 0)
                slide = (uintptr_t)header - (uintptr_t)segment.vmaddr; /* zero: the image is not slid */
            const uintptr_t start = slide + (uintptr_t)segment.vmaddr;
            const bool mapped = (segment.initprot & VM_PROT_READ) != 0
                                && strncmp(segment.segname, SEG_LINKEDIT, sizeof segment.segname) != 0;
            if (mapped && address >= start && address - start < (uintptr_t)segment.vmsize)
                return (segment.initprot & VM_PROT_WRITE) == 0 || in_constant_section(cursor, &segment, slide, address);
        }
        cursor += command.cmdsize;
    }
    return false;
}

/* pthread_get_stackaddr_np gives the stack's top, its highest address, and
 * pthread_get_stacksize_np its size: for the main thread, the stack's
 * resource limit (8 MiB by default). */
int host_image_main_stack(struct host_span *out)
{
    const pthread_t self = pthread_self();
    const uintptr_t top = (uintptr_t)pthread_get_stackaddr_np(self);
    const size_t size = pthread_get_stacksize_np(self);
    if (size == 0 || size > top) {
        (void)fprintf(stderr, "host_image: pthread_get_stackaddr_np and _stacksize_np gave an unusable stack\n");
        return -1;
    }
    *out = (struct host_span){.start = top - size, .end = top};
    return 0;
}

/* --- host_file: the flush ------------------------------------------------------ */

/* fsync(2) on macOS hands the data to the drive and returns; the drive may
 * keep it in its volatile cache, and a power loss then loses a save the game
 * reported written. F_FULLFSYNC asks the drive to write its cache out too
 * (fsync(2): "For applications that require tighter guarantees about the
 * integrity of their data, Mac OS X provides the F_FULLFSYNC fcntl"), which
 * is what docs/C-RULES.md's crash-safe save needs; on a directory it makes the
 * rename into it durable the same way. A file system without it (some network
 * and FAT volumes) refuses the call, and fsync is then the best the system
 * offers; any other error is the caller's to report
 * (host_file_flush_falls_back). Measured on the runner's APFS volume: 4 to 24 ms for the 128 KiB
 * save, 3 to 4 ms for its directory (probe M2). */
int host_file_flush(int descriptor)
{
    if (fcntl(descriptor, F_FULLFSYNC) == 0)
        return 0;
    if (!host_file_flush_falls_back(errno))
        return -1;
    return fsync(descriptor);
}

/* --- host_thread: the monotonic wait --------------------------------------------- */

#define NANOSECONDS_PER_SECOND 1000000000L

/* macOS has no pthread_condattr_setclock: its condition variables' absolute
 * waits are measured on the wall clock. */
int host_condition_init(pthread_cond_t *condition)
{
    return pthread_cond_init(condition, NULL);
}

/* What is left of the deadline on the monotonic clock is waited for as a
 * relative time, which the kernel measures from the call on its own monotonic
 * clock (mach_absolute_time); the wall clock is never read, so a step of it
 * moves nothing. After every wake-up the caller loops, and this computes what
 * is left again. */
int host_condition_wait_until(pthread_cond_t *condition, pthread_mutex_t *lock, const struct timespec *deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return errno;
    struct timespec left = {.tv_sec = deadline->tv_sec - now.tv_sec, .tv_nsec = deadline->tv_nsec - now.tv_nsec};
    if (left.tv_nsec < 0) {
        left.tv_sec--;
        left.tv_nsec += NANOSECONDS_PER_SECOND;
    }
    if (left.tv_sec < 0 || (left.tv_sec == 0 && left.tv_nsec == 0))
        return ETIMEDOUT;
    return pthread_cond_timedwait_relative_np(condition, lock, &left);
}
