/*
 * host_windows.c: the host layer on Windows, a stub until task W5 writes it
 * (research/m8-inventory.md, 5.2). Every function returns its error value
 * and prints nothing: -1, an empty span, or, where the header has no error
 * value, an answer no real one can be mistaken for (UINTPTR_MAX for a register
 * or an address, "read-only" for any address). It
 * exists so that the Windows cross-build links (Makefile.native,
 * TARGET_OS=windows): the executable it makes starts and then fails in the
 * first host call, in gba_memmap_init, and must not be taken for a port.
 * The calls it stands behind are documented in host_fault.h, host_file.h,
 * host_image.h, host_memory.h, host_process.h and host_stack.h, whose
 * "report on stderr" this stub does not make.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "host_fault.h"
#include "host_file.h"
#include "host_image.h"
#include "host_memory.h"
#include "host_process.h"
#include "host_stack.h"

struct host_span host_memory_region(enum gba_region region)
{
    (void)region;
    return (struct host_span){0, 0};
}

struct host_span host_memory_call_stack(void)
{
    return (struct host_span){0, 0};
}

int host_memory_map_fixed(uintptr_t address, size_t size)
{
    (void)address;
    (void)size;
    return -1;
}

int host_memory_unmap_fixed(uintptr_t address, size_t size)
{
    (void)address;
    (void)size;
    return -1;
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

struct host_span host_image_whole(void)
{
    return (struct host_span){0, 0};
}

struct host_span host_image_writable(void)
{
    return (struct host_span){0, 0};
}

bool host_image_is_read_only(uintptr_t address)
{
    (void)address;
    return true;
}

int host_image_main_stack(struct host_span *out)
{
    (void)out;
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
