/*
 * test_rom_mirror.c: the executable's code a second time at its address plus
 * 0x02000000 (rom_mirror.c). The test binary is linked at 0x40000000 like the
 * game, so its code has room above it for the mirror.
 */
#include "ptest.h"
#include "rom_mirror.h"

#include <stdint.h>
#include <string.h>

/* Never inlined and never constant-folded: called through both addresses. */
__attribute__((noinline)) int mirror_test_function(int x)
{
    return x * 7 + 3;
}

static int (*through_the_mirror(void))(int)
{
    return (int (*)(int))((uintptr_t)mirror_test_function + ROM_MIRROR_OFFSET);
}

PTEST(the_offset_is_the_gba_wait_state_1_window)
{
    PT_ASSERT_EQ_U32(ROM_MIRROR_OFFSET, 0x0A000000u - 0x08000000u);
}

PTEST(code_called_through_the_mirror_runs_as_the_original)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    PT_ASSERT_EQ_INT(through_the_mirror()(5), mirror_test_function(5));
    PT_ASSERT_EQ_INT(through_the_mirror()(-2), 3 - 14);
    rom_mirror_shutdown();
}

PTEST(the_mirror_is_the_same_bytes_as_the_original)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    PT_ASSERT_MEM_EQ((const void *)((uintptr_t)mirror_test_function + ROM_MIRROR_OFFSET), (const void *)(uintptr_t)mirror_test_function, 16);
    rom_mirror_shutdown();
}

PTEST(init_twice_is_refused_and_shutdown_repeats)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_ERR_ALREADY);
    PT_ASSERT_EQ_INT(through_the_mirror()(1), 10); /* still mapped after the refused call */
    rom_mirror_shutdown();
    rom_mirror_shutdown();
}

PTEST(shutdown_before_init_is_safe_and_init_works_again_after_shutdown)
{
    rom_mirror_shutdown();
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    rom_mirror_shutdown();
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    PT_ASSERT_EQ_INT(through_the_mirror()(0), 3);
    rom_mirror_shutdown();
}

PTEST(shutdown_gives_the_guard_pages_back_as_zero_filled_memory)
{
    PT_ASSERT_EQ_INT(rom_mirror_init(), ROM_MIRROR_OK);
    rom_mirror_shutdown();
    volatile unsigned char *where = (volatile unsigned char *)((uintptr_t)mirror_test_function + ROM_MIRROR_OFFSET);
    PT_ASSERT_EQ_INT(where[0], 0); /* readable (no hole in the data segment), and not the code */
    where[0] = 0x5A;
    PT_ASSERT_EQ_INT(where[0], 0x5A); /* writable, as the bss it replaced */
    where[0] = 0;
}

PTEST_MAIN()
