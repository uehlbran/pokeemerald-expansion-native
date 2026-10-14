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
 * address. At 64 bits INTR_VECTOR (3007FFCh) is a 32-bit store
 * under the guard (include/gba/defines.h, src/main.c), so it stays inside
 * IWRAM; nothing reads the vector natively.
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
 * --defsym), 0 elsewhere. Weak, so that the compiler cannot take the address
 * of a declared object to be non-null and drop the test of it (it did without
 * the sanitizers, which keep null checks: the Windows unit tests). */
extern const unsigned char gba_game_objects_linked[] __attribute__((weak));

/* gba_game.ld: the .tests table. test/test_runner.c defines
 * gPlatformTestEntrySize (sizeof(struct Test)) in the test runner build only,
 * so the weak reference is null elsewhere. crt0.c cannot include the test
 * headers: it is not compiled with the game's flags (platform/Makefile). */
extern unsigned char __start_tests[], __stop_tests[];
extern const size_t gPlatformTestEntrySize __attribute__((weak));
extern int PlatformTestTableBadEntry(void) __attribute__((weak));

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

int crt0_check_test_table(size_t table_bytes, size_t entry_size, size_t *count)
{
    if (count == NULL || entry_size == 0 || table_bytes % entry_size != 0) {
        return CRT0_ERR_BAD_TEST_TABLE;
    }
    *count = table_bytes / entry_size;
    return CRT0_OK;
}

/* The test runner build's table checks (crt0.h says what each catches),
 * reported on stderr on failure only. */
static int check_tests_table(void)
{
    // cppcheck-suppress knownConditionTrueFalse ; a weak reference, null unless the test runner defines it
    if (&gPlatformTestEntrySize == NULL) {
        return CRT0_OK; /* not the test runner build */
    }
    size_t bytes = span(__start_tests, __stop_tests);
    size_t count = 0;
    if (crt0_check_test_table(bytes, gPlatformTestEntrySize, &count) != CRT0_OK) {
        (void)fprintf(stderr,
                      "crt0: the .tests table is %zu bytes, not a whole number of %zu-byte struct Test entries: "
                      "the entry size the table was built with differs from the runner's (at 64 bits the test "
                      "objects need -malign-data=abi, Makefile.native)\n",
                      bytes, gPlatformTestEntrySize);
        return CRT0_ERR_BAD_TEST_TABLE;
    }
    // cppcheck-suppress knownConditionTrueFalse ; a weak reference, as above
    int bad = &PlatformTestTableBadEntry != NULL ? PlatformTestTableBadEntry() : -1;
    if (bad >= 0) {
        (void)fprintf(stderr,
                      "crt0: .tests entry %d (of %zu) has a null name, filename or runner: padding between the "
                      "entries (at 64 bits the test objects need -malign-data=abi, Makefile.native)\n",
                      bad, count);
        return CRT0_ERR_BAD_TEST_TABLE;
    }
    return CRT0_OK;
}

int crt0_init(void)
{
    crt0_shutdown(); /* a second init, or a failed one, never leaves an older copy in use */
    if ((uintptr_t)gba_game_objects_linked != 0 && span(game_bss_start, game_bss_objects_end) == 0) {
        (void)fprintf(stderr,
                      "crt0: no .bss from the game's objects in game_bss: gba_game.ld's object-path patterns "
                      "do not match Makefile.native's object directories, so a soft reset would leave the game's "
                      "variables stale\n");
        return CRT0_ERR_NO_GAME_OBJECTS;
    }
    if (check_tests_table() != CRT0_OK) {
        return CRT0_ERR_BAD_TEST_TABLE;
    }
    size_t ewram_size = crt0_ewram_init_size();
    size_t data_size = crt0_game_data_size();
    if (ewram_size > sizeof copies.ewram_init) {
        (void)fprintf(stderr, "crt0: the EWRAM_INIT data is %zu bytes, more than CRT0_EWRAM_INIT_MAX_SIZE (%u)\n",
                      ewram_size, CRT0_EWRAM_INIT_MAX_SIZE);
        return CRT0_ERR_EWRAM_INIT_SIZE;
    }
    if (data_size > 0) {
        copies.game_data = malloc(data_size);
        if (copies.game_data == NULL) {
            (void)fprintf(stderr, "crt0: malloc of %zu bytes for the game's initialised data failed\n", data_size);
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
