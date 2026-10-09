/*
 * test_script_tag.c: the tag natively carried by event script function
 * pointers (script_tag.h).
 */
#include "ptest.h"
#include "script_tag.h"

static int sample(int x)
{
    return x + 1;
}

PTEST(the_tag_is_the_roms_second_window_offset)
{
    PT_ASSERT_EQ_U32(SCRIPT_TAG, 0x0A000000u - 0x08000000u);
}

PTEST(a_code_address_is_untagged_and_a_tagged_one_is_tagged)
{
    uintptr_t code = (uintptr_t)sample;
    PT_ASSERT(!script_tag_is_set(code));
    PT_ASSERT(script_tag_is_set(code + SCRIPT_TAG));
}

PTEST(strip_removes_the_tag_and_leaves_an_untagged_pointer_alone)
{
    uintptr_t code = (uintptr_t)sample;
    PT_ASSERT(script_tag_strip(code + SCRIPT_TAG) == code);
    PT_ASSERT(script_tag_strip(code) == code);
    PT_ASSERT(script_tag_strip(0) == 0);
}

PTEST(a_tagged_function_pointer_untags_to_a_callable_one)
{
    int (*tagged)(int) = (int (*)(int))((uintptr_t)sample + SCRIPT_TAG);
    int (*plain)(int) = SCRIPT_UNTAG(tagged);
    PT_ASSERT(plain == sample);
    PT_ASSERT_EQ_INT(plain(4), 5);
}

PTEST(the_native_code_is_below_the_first_tagged_address)
{
    /* the link address (gba_memmap.h: the executable is at 0x40000000) leaves bit 25 clear */
    PT_ASSERT((uintptr_t)sample < 0x42000000u);
    PT_ASSERT(!script_tag_is_set((uintptr_t)sample));
}

PTEST_MAIN()
