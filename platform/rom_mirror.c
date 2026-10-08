/*
 * rom_mirror.c: the executable code at its address plus the ROM mirror
 * offset (rom_mirror.h).
 *
 * Finds the program's executable PT_LOAD segment through dl_iterate_phdr and
 * maps the same pages of the program file at vaddr + ROM_MIRROR_OFFSET. That
 * address lies in the zero-filled rom_mirror_guard section gba_game.ld
 * reserves (so that the heap cannot be there); the mapping replaces those
 * pages with MAP_FIXED, after checking that the whole range is inside the
 * guard. If it is not (a link without the guard), MAP_FIXED_NOREPLACE
 * refuses to replace anything; an older kernel that ignores that flag may
 * place the mapping elsewhere, which is detected and undone.
 */
#define _GNU_SOURCE
#include "rom_mirror.h"

#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ROM_MIRROR_EXE_PATH "/proc/self/exe"

/* gba_game.ld: the zero-filled space reserved for the mirror. */
extern char rom_mirror_guard_start[];
extern char rom_mirror_guard_end[];

struct text_segment {
    uintptr_t address; /* page aligned */
    size_t length;     /* whole pages */
    off_t offset;      /* in the file, page aligned */
    bool found;
};

static struct {
    void *base;
    size_t length;
    bool in_guard; /* the mapping replaced guard pages, which shutdown restores */
} mapped;

/* dl_iterate_phdr callback: the first object is the program. */
static int find_text(struct dl_phdr_info *info, size_t size, void *data)
{
    struct text_segment *text = data;
    (void)size;
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    for (size_t i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *header = &info->dlpi_phdr[i];
        if (header->p_type != PT_LOAD || (header->p_flags & PF_X) == 0)
            continue;
        const uintptr_t start = (uintptr_t)info->dlpi_addr + header->p_vaddr;
        const uintptr_t end = start + header->p_memsz;
        const uintptr_t aligned_start = start & ~(uintptr_t)(page - 1);
        text->address = aligned_start;
        text->length = (size_t)(((end + page - 1) & ~(uintptr_t)(page - 1)) - aligned_start);
        text->offset = (off_t)(header->p_offset & ~(size_t)(page - 1));
        text->found = true;
        break;
    }
    return 1; /* only the program */
}

int rom_mirror_init(void)
{
    struct text_segment text = { 0 };
    if (mapped.base != NULL) {
        fprintf(stderr, "rom_mirror: already initialised\n");
        return ROM_MIRROR_ERR_ALREADY;
    }
    (void)dl_iterate_phdr(find_text, &text);
    if (!text.found) {
        fprintf(stderr, "rom_mirror: no executable segment in the program headers\n");
        return ROM_MIRROR_ERR_NO_TEXT;
    }
    int fd = open(ROM_MIRROR_EXE_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "rom_mirror: open(%s) failed: %s\n", ROM_MIRROR_EXE_PATH, strerror(errno));
        return ROM_MIRROR_ERR_OPEN;
    }
    void *wanted = (void *)(text.address + ROM_MIRROR_OFFSET);
    const bool in_guard = text.address + ROM_MIRROR_OFFSET >= (uintptr_t)rom_mirror_guard_start
                          && text.address + ROM_MIRROR_OFFSET + text.length <= (uintptr_t)rom_mirror_guard_end;
    void *got = mmap(wanted, text.length, PROT_READ | PROT_EXEC,
                     MAP_PRIVATE | (in_guard ? MAP_FIXED : MAP_FIXED_NOREPLACE), fd, text.offset);
    int saved_errno = errno;
    close(fd);
    if (got == MAP_FAILED || got != wanted) {
        if (got != MAP_FAILED)
            munmap(got, text.length);
        fprintf(stderr, "rom_mirror: cannot map the code at %p: %s\n", wanted,
                got == MAP_FAILED ? strerror(saved_errno) : "the address is taken");
        return ROM_MIRROR_ERR_MAP;
    }
    mapped.base = got;
    mapped.length = text.length;
    mapped.in_guard = in_guard;
    return ROM_MIRROR_OK;
}

/* Gives the guard's zero-filled pages back where the mirror was, so the data
 * segment is whole again: tools that scan it (LeakSanitizer reads every
 * writable segment) would fault on the hole an unmap leaves. */
static void restore_guard(void)
{
    void *restored = mmap(mapped.base, mapped.length, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (restored == MAP_FAILED)
        fprintf(stderr, "rom_mirror: cannot restore the guard pages: %s\n", strerror(errno));
}

void rom_mirror_shutdown(void)
{
    if (mapped.base != NULL) {
        if (mapped.in_guard)
            restore_guard();
        else
            munmap(mapped.base, mapped.length);
    }
    mapped.base = NULL;
    mapped.length = 0;
    mapped.in_guard = false;
}
