/*
 * host_image.h: where the executable's code and data, and the main thread's
 * stack, lie in memory.
 *
 * Stands in for the parts of the GBA memory map that hold the game itself:
 * natively the cartridge ROM's contents and the game's variables are the
 * executable's segments, wherever the link put them. transfer_bounds asks
 * which of them a transfer may touch, and PlatformIsRomPointer (game_hooks)
 * which are read-only, as the ROM was.
 *
 * Wraps, on Linux (host_linux.c): GNU ld's __executable_start and _end and
 * glibc's __data_start (crt1.o, -m32 and -m64), the program headers through
 * getauxval(AT_PHDR), and pthread_getattr_np for the main thread's stack. On
 * Windows (host_windows.c): the symbols around the PE image's code and data
 * (gba_game.pe.ld's __text_start__, MinGW's __data_start__ and __bss_end__,
 * with the writable sections linked last), the pages' protection through
 * VirtualQuery, and GetCurrentThreadStackLimits for the main thread's stack,
 * 2 MiB there (the image's stack reserve) against Linux's 8 MiB.
 */
#ifndef PLATFORM_HOST_IMAGE_H
#define PLATFORM_HOST_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "host_memory.h" /* struct host_span */

/* The executable's image: its first byte to the end of its zero-initialised
 * data (code, read-only data, data, bss). On Windows the image starts at its
 * code: the PE headers and the GBA layout below the code (the regions and
 * the gaps between them, gba_regions.pe.ld) are not the program's. */
struct host_span host_image_whole(void);

/* The image's writable data: initialised and zero-initialised, to the
 * image's end. */
struct host_span host_image_writable(void);

/* True if address lies in one of the executable's read-only loaded segments
 * (code, read-only data, string literals); false for its writable data and
 * for any memory outside it. */
bool host_image_is_read_only(uintptr_t address);

/* The main thread's whole stack mapping, not just its current depth. Call it
 * on the main thread, outside host_stack_run. Returns 0, or -1 after a report
 * on stderr naming the call that failed. */
int host_image_main_stack(struct host_span *out);

#endif /* PLATFORM_HOST_IMAGE_H */
