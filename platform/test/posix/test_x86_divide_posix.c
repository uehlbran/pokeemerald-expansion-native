/*
 * test_x86_divide_posix.c: x86_divide_at reads no further than the
 * instruction needs (x86_divide.h). The crash handler asks it about the
 * instruction at a SIGFPE's program counter, which may be a one-byte
 * instruction ending the last mapped page of code; the byte after it is not
 * there to read. The page after the tested bytes is made inaccessible here,
 * so a read past them faults. The byte strings themselves are tested in
 * ../test_x86_divide.c.
 */
// deps: x86_divide.c
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ptest.h"
#include "x86_divide.h"

/* Two pages, the second inaccessible; NULL if that cannot be had. */
static unsigned char *page_before_a_hole(size_t page)
{
    void *pages = mmap(NULL, 2u * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pages == MAP_FAILED)
        return NULL;
    unsigned char *bytes = pages;
    if (mprotect(bytes + page, page, PROT_NONE) != 0) {
        (void)munmap(pages, 2u * page);
        return NULL;
    }
    return bytes;
}

PTEST(a_one_byte_instruction_at_the_end_of_the_mapped_memory_is_read_alone)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    PT_ASSERT(page_size > 0);
    const size_t page = (size_t)page_size;
    unsigned char *bytes = page_before_a_hole(page);
    PT_ASSERT(bytes != NULL);
    memset(bytes, 0x90, page);
    const bool nop = x86_divide_at(&bytes[page - 1u]); /* nop, the last byte */
    bytes[page - 2u] = 0x48;                           /* REX, then nop */
    const bool rex_nop = x86_divide_at(&bytes[page - 2u]);
    bytes[page - 2u] = 0xF7; /* div ecx, ending the page */
    bytes[page - 1u] = 0xF1;
    const bool division = x86_divide_at(&bytes[page - 2u]);
    PT_ASSERT_EQ_INT(munmap(bytes, 2u * page), 0);
    PT_ASSERT(!nop);
    PT_ASSERT(!rex_nop);
    PT_ASSERT(division);
}

PTEST_MAIN()
