/*
 * test_rom_mirror.c: the Game Pak ROM's first mirror (rom_mirror.h).
 *
 * Expected behaviour from GBATEK "GBA Memory Map" (0A000000h mirrors the ROM
 * at 08000000h) and from what expansion does with it: call a function at
 * its address plus ROM_SIZE (2000000h) and recognise such an address by
 * (address & E000000h) == A000000h (src/script.c, include/script.h). This
 * binary is linked at 08000000h like every platform test (platform/Makefile).
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ptest.h"
#include "rom_mirror.h"

/* include/script.h and src/script.c: "In ROM mirror 1". */
#define EXPANSION_MIRROR_MASK UINT32_C(0x0E000000)
#define EXPANSION_MIRROR_1 UINT32_C(0x0A000000)
#define EXPANSION_ROM UINT32_C(0x08000000)

static volatile int calls_seen;
static const unsigned char rom_bytes[] = { 0x13, 0x37, 0xC0, 0xDE, 0x5A };

static __attribute__((noinline)) int add_seven(int x)
{
    calls_seen++; /* a global: the mirror must reach the one copy */
    return x + 7;
}

static __attribute__((noinline)) size_t measure(const char *text)
{
    char copy[32];
    snprintf(copy, sizeof copy, "%s!", text); /* through the PLT */
    return strlen(copy);
}

static uintptr_t address_of_function(int (*function)(int))
{
    uintptr_t address;
    memcpy(&address, &function, sizeof address);
    return address;
}

static int (*mirrored_add(void))(int)
{
    uintptr_t address = address_of_function(add_seven) + ROM_MIRROR_OFFSET;
    int (*function)(int);
    memcpy(&function, &address, sizeof function);
    return function;
}

static bool mapped(const void *address)
{
    unsigned char resident;
    uintptr_t page = (uintptr_t)address & ~((uintptr_t)sysconf(_SC_PAGESIZE) - 1u);
    return mincore((void *)page, 1, &resident) == 0;
}

#if UINTPTR_MAX == UINT32_MAX

PTEST(code_runs_the_same_from_the_mirror)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    calls_seen = 0;
    int direct = add_seven(1);
    int mirrored = mirrored_add()(35);
    int seen = calls_seen;
    rom_mirror_shutdown();
    PT_ASSERT_EQ_INT(direct, 8);
    PT_ASSERT_EQ_INT(mirrored, 42);
    PT_ASSERT_EQ_INT(seen, 2);
}

PTEST(code_in_the_mirror_calls_the_c_library)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    uintptr_t address;
    size_t (*function)(const char *) = measure;
    memcpy(&address, &function, sizeof address);
    address += ROM_MIRROR_OFFSET;
    memcpy(&function, &address, sizeof function);
    size_t length = function("mirror");
    rom_mirror_shutdown();
    PT_ASSERT(length == 7);
}

PTEST(read_only_data_reads_the_same_in_the_mirror)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    const volatile unsigned char *mirrored = (const volatile unsigned char *)((uintptr_t)rom_bytes + ROM_MIRROR_OFFSET);
    unsigned char copy[sizeof rom_bytes];
    for (size_t i = 0; i < sizeof copy; i++)
        copy[i] = mirrored[i];
    rom_mirror_shutdown();
    PT_ASSERT_MEM_EQ(copy, rom_bytes, sizeof rom_bytes);
}

PTEST(expansion_tells_a_mirror_address_from_a_rom_one)
{
    uintptr_t rom = address_of_function(add_seven);
    PT_ASSERT_EQ_U32((uint32_t)(rom & EXPANSION_MIRROR_MASK), EXPANSION_ROM);
    PT_ASSERT_EQ_U32((uint32_t)((rom + ROM_MIRROR_OFFSET) & EXPANSION_MIRROR_MASK), EXPANSION_MIRROR_1);
}

PTEST(writable_data_is_not_mirrored)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    bool data_mirrored = mapped((const void *)((uintptr_t)&calls_seen + ROM_MIRROR_OFFSET));
    bool code_mirrored = mapped((const void *)(address_of_function(add_seven) + ROM_MIRROR_OFFSET));
    rom_mirror_shutdown();
    PT_ASSERT(!data_mirrored);
    PT_ASSERT(code_mirrored);
}

PTEST(shutdown_unmaps_and_init_maps_again)
{
    const void *mirror_of_code = (const void *)(address_of_function(add_seven) + ROM_MIRROR_OFFSET);
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK); /* again: unmaps first, no EEXIST */
    bool while_on = mapped(mirror_of_code);
    rom_mirror_shutdown();
    bool after = mapped(mirror_of_code);
    rom_mirror_shutdown(); /* twice is safe */
    PT_ASSERT(while_on);
    PT_ASSERT(!after);
}

PTEST(init_fails_cleanly_when_the_mirror_is_taken)
{
    /* Something already at the mirror of the code: init reports and leaves
     * nothing mapped of its own. */
    long page_size = sysconf(_SC_PAGESIZE);
    void *code_mirror = (void *)((address_of_function(add_seven) + ROM_MIRROR_OFFSET) & ~((uintptr_t)page_size - 1u));
    void *squatter = mmap(code_mirror, (size_t)page_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                          -1, 0);
    PT_ASSERT(squatter == code_mirror);
    int result = rom_mirror_init();
    munmap(squatter, (size_t)page_size);
    PT_ASSERT_EQ_INT(result, ROM_MIRROR_ERR_MAP);
    /* The headers' segment at the ROM's base comes before the code and was
     * mapped first; it is gone again, and the read-only data never came. */
    PT_ASSERT(!mapped((const void *)(uintptr_t)(ROM_MIRROR_ROM_BASE + ROM_MIRROR_OFFSET)));
    PT_ASSERT(!mapped((const void *)((uintptr_t)rom_bytes + ROM_MIRROR_OFFSET)));
}

#else

PTEST(a_64_bit_build_has_no_mirror)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_ERR_64_BIT);
    PT_ASSERT(!mapped((const void *)(address_of_function(add_seven) + ROM_MIRROR_OFFSET)));
    PT_ASSERT_EQ_INT(add_seven(0), 7);
    PT_ASSERT(measure("x") == 2 && rom_bytes[0] == 0x13 && mirrored_add != NULL);
    rom_mirror_shutdown();
}

#endif

PTEST_MAIN()
