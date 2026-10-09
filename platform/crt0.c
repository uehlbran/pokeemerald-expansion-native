/*
 * crt0.c: the parts of expansion's src/crt0.s the game links against.
 *
 * Stands in for ROM start-up code, not hardware (crt0.h). Followed:
 * src/crt0.s.
 *
 * IntrMain. crt0.s's IntrMain is the IRQ handler the BIOS jumps to through
 * the pointer at 3007FFCh, and src/main.c InitIntrHandlers stores its
 * address there (INTR_VECTOR = IntrMain). Natively nothing jumps there: the
 * interrupt module dispatches gIntrTable itself (interrupt.h), so IntrMain is
 * data whose address is taken. It is CRT0_INTR_MAIN_SIZE bytes, the size of
 * the IntrMain_Buffer pokeemerald copied it into, so a version of expansion
 * that copies it again reads defined bytes. Expansion 1.17.1 only takes the
 * address. 64-bit: INTR_VECTOR is a void * at 3007FFCh, so storing the
 * address writes 8 bytes, past the end of IWRAM (flagged in
 * platform/build_glue/LINK-NEEDS.md).
 *
 * ReInitializeEWRAM. crt0.s copies the .ewram data (EWRAM_INIT) from its
 * load address in ROM back into EWRAM. Natively that data's only initial
 * image is what the loader mapped, which the game then changes, so
 * crt0_init keeps a copy (gba_regions.ld places the data between
 * gba_linked_ewram_init_start and _end).
 */
#include "crt0.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* gba_regions.ld: the EWRAM_INIT data, as loaded. gba_game.ld: the game's
 * initialised and zero-initialised variables. */
extern unsigned char gba_linked_ewram_init_start[], gba_linked_ewram_init_end[];
extern unsigned char game_data_start[], game_data_end[];
extern unsigned char game_bss_start[], game_bss_objects_end[], game_bss_end[];
/* gba_game.ld: its address is 1 in the game executable (Makefile.native's
 * --defsym), 0 elsewhere. */
extern const unsigned char gba_game_objects_linked[];

const uint32_t IntrMain[CRT0_INTR_MAIN_SIZE / sizeof(uint32_t)];

static struct {
    unsigned char ewram_init[CRT0_EWRAM_INIT_MAX_SIZE];
    size_t ewram_init_size;
    unsigned char *game_data; /* malloc'd by crt0_init, freed by crt0_shutdown */
    size_t game_data_size;
    bool initialised;
} copies;

/* Bytes between two linker symbols bracketing one section, compared as
 * addresses. */
static size_t span(const unsigned char *start, const unsigned char *end)
{
    return (size_t)((uintptr_t)end - (uintptr_t)start);
}

/* Byte by byte through volatile pointers, and not instrumented: in the
 * sanitized build the game's globals carry AddressSanitizer redzones, and
 * these copy and clear whole sections, redzones included, as the GBA's crt0
 * copies and clears whole RAM areas; memset and memcpy would be intercepted
 * and checked against the redzones. */
__attribute__((no_sanitize("address"))) static void zero_bytes(unsigned char *dest, size_t size)
{
    volatile unsigned char *out = dest;
    for (size_t i = 0; i < size; i++)
        out[i] = 0;
}

__attribute__((no_sanitize("address"))) static void copy_bytes(unsigned char *dest, const unsigned char *src,
                                                                 size_t size)
{
    volatile unsigned char *out = dest;
    for (size_t i = 0; i < size; i++)
        out[i] = src[i];
}

size_t crt0_ewram_init_size(void)
{
    return span(gba_linked_ewram_init_start, gba_linked_ewram_init_end);
}

size_t crt0_game_data_size(void)
{
    return span(game_data_start, game_data_end);
}

size_t crt0_game_bss_size(void)
{
    return span(game_bss_start, game_bss_end);
}

int crt0_init(void)
{
    crt0_shutdown(); /* a second init, or a failed one, never leaves an older copy in use */
    if ((uintptr_t)gba_game_objects_linked != 0 && span(game_bss_start, game_bss_objects_end) == 0) {
        fprintf(stderr, "crt0: no .bss from the game's objects in game_bss: gba_game.ld's object-path patterns "
                "do not match Makefile.native's object directories, so a soft reset would leave the game's "
                "variables stale\n");
        return CRT0_ERR_NO_GAME_OBJECTS;
    }
    size_t ewram_size = crt0_ewram_init_size();
    size_t data_size = crt0_game_data_size();
    if (ewram_size > sizeof copies.ewram_init) {
        fprintf(stderr, "crt0: the EWRAM_INIT data is %zu bytes, more than CRT0_EWRAM_INIT_MAX_SIZE (%u)\n",
                ewram_size, CRT0_EWRAM_INIT_MAX_SIZE);
        return CRT0_ERR_EWRAM_INIT_SIZE;
    }
    if (data_size > 0) {
        copies.game_data = malloc(data_size);
        if (copies.game_data == NULL) {
            fprintf(stderr, "crt0: malloc of %zu bytes for the game's initialised data failed\n", data_size);
            return CRT0_ERR_NO_MEMORY;
        }
    }
    copy_bytes(copies.ewram_init, gba_linked_ewram_init_start, ewram_size);
    copies.ewram_init_size = ewram_size;
    copy_bytes(copies.game_data, game_data_start, data_size);
    copies.game_data_size = data_size;
    copies.initialised = true;
    return CRT0_OK;
}

void crt0_shutdown(void)
{
    free(copies.game_data);
    copies.game_data = NULL;
    copies.game_data_size = 0;
    copies.ewram_init_size = 0;
    copies.initialised = false;
}

void ReInitializeEWRAM(void)
{
    if (!copies.initialised)
        return;
    copy_bytes(gba_linked_ewram_init_start, copies.ewram_init, copies.ewram_init_size);
}

void crt0_restart(void)
{
    if (!copies.initialised)
        return;
    zero_bytes(game_bss_start, crt0_game_bss_size());
    copy_bytes(game_data_start, copies.game_data, copies.game_data_size);
    copy_bytes(gba_linked_ewram_init_start, copies.ewram_init, copies.ewram_init_size);
}
