/*
 * host_windows.c: the host layer on Windows (x86-64, MinGW-w64): the pages
 * mapped at fixed addresses (host_memory.h) and the image's and the main
 * stack's bounds (host_image.h); where the link put the regions is
 * host_linked.c's. Until tasks W5 to W7 have written each part, the other
 * calls are stubs (research/m8-inventory.md, 5.2) that
 * return their error value and print nothing: -1, an empty span, or, where
 * the header has no error value, an answer no real one can be mistaken for
 * (UINTPTR_MAX for a register or an address, "read-only" for any address).
 *
 * Followed: Microsoft's documentation of VirtualAlloc, VirtualFree and
 * VirtualQuery (memoryapi.h), GetCurrentThreadStackLimits
 * (processthreadsapi.h) and FormatMessage for the reports; the PE format's
 * section flags through gba_game.pe.ld's layout.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <windows.h>

#include "host_fault.h"
#include "host_file.h"
#include "host_image.h"
#include "host_memory.h"
#include "host_process.h"
#include "host_stack.h"

/* --- reports ---------------------------------------------------------------------- */

/* Room for the system's text of an error code. */
#define WINDOWS_ERROR_TEXT_SIZE 256u

/* The system's text for a GetLastError code, without its line break, or
 * "unknown error" if it has none. */
static const char *windows_error_text(DWORD error, char *text, DWORD size)
{
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0,
                                        text, size, NULL);
    DWORD end = length;
    while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == '.'))
        end--;
    if (end == 0)
        return "unknown error";
    text[end] = '\0';
    return text;
}

/* "<subsystem>: <call> at 0x<address> failed: <the system's text> (Windows
 * error <code>)", for the call that just failed. */
static void report_memory_failure(const char *call, uintptr_t address)
{
    const DWORD error = GetLastError();
    char text[WINDOWS_ERROR_TEXT_SIZE];
    (void)fprintf(stderr, "host_memory: %s at 0x%08" PRIXPTR " failed: %s (Windows error %lu)\n", call, address,
                  windows_error_text(error, text, sizeof text), (unsigned long)error);
}

/* --- host_memory: pages mapped at run time ---------------------------------------- */

/* VirtualAlloc reserves address space in units of the allocation granularity,
 * 64 KiB on every Windows so far (GetSystemInfo's dwAllocationGranularity),
 * and rounds a reservation's address down to a multiple of it: an address
 * that is not a multiple of 64 KiB comes back elsewhere and is refused, and a
 * mapping keeps the rest of its 64 KiB unit from any other. MEM_RESERVE
 * fails if any page of the range is reserved already, which is the refusal
 * to map over a mapping (Linux: MAP_FIXED_NOREPLACE). */
int host_memory_map_fixed(uintptr_t address, size_t size)
{
    void *const wanted = (void *)address;
    void *const got = VirtualAlloc(wanted, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (got == NULL) {
        report_memory_failure("VirtualAlloc", address);
        return -1;
    }
    if (got != wanted) {
        (void)fprintf(stderr,
                      "host_memory: VirtualAlloc at 0x%08" PRIXPTR " returned %p (not a multiple of the 64 KiB "
                      "allocation granularity)\n",
                      address, got);
        if (!VirtualFree(got, 0, MEM_RELEASE))
            report_memory_failure("VirtualFree", (uintptr_t)got);
        return -1;
    }
    return 0;
}

/* VirtualFree releases a whole reservation, never part of one, so the size
 * must be the one host_memory_map_fixed was given (whole pages, as both
 * calls take it); anything else is refused rather than half done. */
int host_memory_unmap_fixed(uintptr_t address, size_t size)
{
    void *const base = (void *)address;
    MEMORY_BASIC_INFORMATION mapping;
    if (VirtualQuery(base, &mapping, sizeof mapping) != sizeof mapping) {
        report_memory_failure("VirtualQuery", address);
        return -1;
    }
    if (mapping.AllocationBase != base || mapping.RegionSize != size) {
        (void)fprintf(stderr,
                      "host_memory: 0x%08" PRIXPTR " is not a mapping of %zu bytes made by host_memory_map_fixed\n",
                      address, size);
        return -1;
    }
    if (!VirtualFree(base, 0, MEM_RELEASE)) {
        report_memory_failure("VirtualFree", address);
        return -1;
    }
    return 0;
}

/* --- host_image ------------------------------------------------------------------------ */

/* gba_game.pe.ld's start of the code, where the program's image begins above
 * the GBA layout; MinGW's default script's end of the zero-initialised data;
 * and its start of the initialised data, which gba_game.pe.ld moves after
 * every read-only section (check_pe_layout.py checks the order on each link),
 * so that from it to the end everything is writable. */
extern const unsigned char __text_start__[], __data_start__[], __bss_end__[];

/* Pointers to integers: the image's bounds are compared with addresses. */
static struct host_span span_of(const unsigned char *start, const unsigned char *end)
{
    return (struct host_span){.start = (uintptr_t)start, .end = (uintptr_t)end};
}

struct host_span host_image_whole(void)
{
    return span_of(__text_start__, __bss_end__);
}

struct host_span host_image_writable(void)
{
    return span_of(__data_start__, __bss_end__);
}

/* The low byte of a page's protection is its access; the bits above it
 * (PAGE_GUARD, PAGE_NOCACHE, PAGE_WRITECOMBINE) modify it (winnt.h). */
#define PAGE_ACCESS_MASK 0xFFu

/* The page's own protection, which the loader set from its section's flags:
 * the game's initialised data (.gamedat) lies below __data_start__ and is
 * writable, so the bounds alone cannot say. Copy-on-write pages, which the
 * image's writable sections are until first written, count as writable. */
bool host_image_is_read_only(uintptr_t address)
{
    const struct host_span image = host_image_whole();
    MEMORY_BASIC_INFORMATION page;
    if (address < image.start || address >= image.end
        || VirtualQuery((const void *)address, &page, sizeof page) != sizeof page)
        return false;
    const DWORD access = page.Protect & PAGE_ACCESS_MASK;
    return page.State == MEM_COMMIT && (page.Protect & PAGE_GUARD) == 0
           && (access == PAGE_READONLY || access == PAGE_EXECUTE_READ);
}

/* The thread's whole stack reservation, from the thread's environment block
 * (Windows 8 and later), not just its committed depth: 2 MiB for the main
 * thread, the image's SizeOfStackReserve (MinGW's default), against Linux's
 * 8 MiB. */
int host_image_main_stack(struct host_span *out)
{
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    if (low == 0 || high <= low) {
        (void)fprintf(stderr, "host_image: GetCurrentThreadStackLimits returned an unusable stack\n");
        return -1;
    }
    *out = (struct host_span){.start = (uintptr_t)low, .end = (uintptr_t)high};
    return 0;
}

int host_fault_install_divide_error(host_fault_divide_handler handler)
{
    (void)handler;
    return -1;
}

void host_fault_uninstall_divide_error(void)
{
}

int host_fault_install_crash(host_fault_crash_handler handler)
{
    (void)handler;
    return -1;
}

void host_fault_uninstall_crash(void)
{
}

bool host_fault_is_divide_error(const struct host_fault *fault)
{
    (void)fault;
    return false;
}

int host_fault_number(const struct host_fault *fault)
{
    (void)fault;
    return -1;
}

uintptr_t host_fault_address(const struct host_fault *fault)
{
    (void)fault;
    return UINTPTR_MAX;
}

uintptr_t host_fault_register(const struct host_fault *fault, enum host_fault_register name)
{
    (void)fault;
    (void)name;
    return UINTPTR_MAX;
}

void host_fault_set_register(struct host_fault *fault, enum host_fault_register name, uintptr_t value)
{
    (void)fault;
    (void)name;
    (void)value;
}

uintptr_t host_fault_general_register(const struct host_fault *fault, unsigned number)
{
    (void)fault;
    (void)number;
    return UINTPTR_MAX;
}

int host_process_executable_path(char *out, size_t size)
{
    (void)out;
    (void)size;
    return -1;
}

int host_process_restart(char *const argv[])
{
    (void)argv;
    return -1;
}

int host_stack_run(void *stack, size_t size, void (*entry)(void *context), void *context)
{
    (void)stack;
    (void)size;
    (void)entry;
    (void)context;
    return -1;
}

void host_stack_leave(void)
{
}

int host_file_sync(int descriptor)
{
    (void)descriptor;
    return -1;
}

int host_file_replace(const char *from, const char *path)
{
    (void)from;
    (void)path;
    return -1;
}

int host_file_sync_directory_of(const char *path)
{
    (void)path;
    return -1;
}

int host_file_make_directory(const char *path)
{
    (void)path;
    return -1;
}

int host_file_local_time(int64_t seconds, struct tm *out)
{
    (void)seconds;
    (void)out;
    return -1;
}
