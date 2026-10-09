/*
 * rom_mirror.c: the Game Pak ROM's first mirror (rom_mirror.h has the
 * contract and why expansion needs it).
 *
 * Followed: GBATEK "GBA Memory Map" (the ROM at 08000000h, mirrored at
 * 0A000000h and 0C000000h); the ELF program headers (elf(5)) for which parts
 * of the executable the ROM window holds.
 */
#include "rom_mirror.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

#if UINTPTR_MAX == UINT32_MAX
typedef Elf32_Phdr program_header;
#else
typedef Elf64_Phdr program_header;
#endif

struct mapping {
    void *address;
    size_t length;
};

static struct {
    struct mapping mappings[ROM_MIRROR_MAX_SEGMENTS];
    size_t count;
} mirror;

/* A read-only loadable segment, page-aligned, and where its bytes are in the
 * file. Flagged for 64-bit: addresses as integers, as the program headers
 * hold them. */
struct segment {
    uintptr_t start;
    size_t length;
    off_t file_offset;
    int protection;
};

static bool in_rom_window(uintptr_t address)
{
    return address >= ROM_MIRROR_ROM_BASE && address < ROM_MIRROR_ROM_BASE + ROM_MIRROR_ROM_SIZE;
}

static bool page_aligned_segment(const program_header *header, uintptr_t page_size, struct segment *out)
{
    uintptr_t start = (uintptr_t)header->p_vaddr & ~(page_size - 1u);
    uintptr_t end = ((uintptr_t)header->p_vaddr + (uintptr_t)header->p_filesz + page_size - 1u) & ~(page_size - 1u);
    uintptr_t lead = (uintptr_t)header->p_vaddr - start;
    if ((uintptr_t)header->p_offset < lead)
        return false;
    out->start = start;
    out->length = (size_t)(end - start);
    out->file_offset = (off_t)((uintptr_t)header->p_offset - lead);
    out->protection = PROT_READ | ((header->p_flags & PF_X) != 0 ? PROT_EXEC : 0);
    return true;
}

static int map_segment(int fd, const struct segment *segment)
{
    void *wanted = (void *)(segment->start + ROM_MIRROR_OFFSET);
    void *mapped = mmap(wanted, segment->length, segment->protection, MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd,
                        segment->file_offset);
    if (mapped == MAP_FAILED) {
        fprintf(stderr, "rom_mirror: mmap of %zu bytes at %p failed: %s\n", segment->length, wanted, strerror(errno));
        return ROM_MIRROR_ERR_MAP;
    }
    if (mapped != wanted) { /* a kernel without MAP_FIXED_NOREPLACE treats it as a hint */
        munmap(mapped, segment->length);
        fprintf(stderr, "rom_mirror: the mirror could not be placed at %p\n", wanted);
        return ROM_MIRROR_ERR_MAP;
    }
    mirror.mappings[mirror.count++] = (struct mapping){ mapped, segment->length };
    return ROM_MIRROR_OK;
}

/* Maps each read-only segment in the ROM window; ROM_MIRROR_ERR_LAYOUT if
 * there is none, or one that leaves the window or overlaps the mirror. */
static int map_read_only_segments(int fd)
{
    const program_header *headers = (const program_header *)(uintptr_t)getauxval(AT_PHDR);
    size_t count = (size_t)getauxval(AT_PHNUM);
    uintptr_t page_size = (uintptr_t)sysconf(_SC_PAGESIZE);
    if (headers == NULL || count == 0) {
        fprintf(stderr, "rom_mirror: the program headers are not available (getauxval)\n");
        return ROM_MIRROR_ERR_LAYOUT;
    }
    for (size_t i = 0; i < count; i++) {
        const program_header *header = &headers[i];
        struct segment segment;
        if (header->p_type != PT_LOAD || (header->p_flags & PF_W) != 0 || !in_rom_window((uintptr_t)header->p_vaddr))
            continue;
        if (!page_aligned_segment(header, page_size, &segment) || segment.length == 0)
            continue;
        if (segment.start + segment.length > ROM_MIRROR_ROM_BASE + ROM_MIRROR_ROM_SIZE) {
            fprintf(stderr, "rom_mirror: a read-only segment (%p, %zu bytes) runs past the 32 MiB ROM window\n",
                    (void *)segment.start, segment.length);
            return ROM_MIRROR_ERR_LAYOUT;
        }
        if (mirror.count == ROM_MIRROR_MAX_SEGMENTS) {
            fprintf(stderr, "rom_mirror: more than %u read-only segments\n", ROM_MIRROR_MAX_SEGMENTS);
            return ROM_MIRROR_ERR_LAYOUT;
        }
        int result = map_segment(fd, &segment);
        if (result != ROM_MIRROR_OK)
            return result;
    }
    if (mirror.count == 0) {
        fprintf(stderr, "rom_mirror: no read-only segment in the ROM window: the executable is not linked at "
                "08000000h (-Wl,-Ttext-segment)\n");
        return ROM_MIRROR_ERR_LAYOUT;
    }
    return ROM_MIRROR_OK;
}

int rom_mirror_init(void)
{
    rom_mirror_shutdown();
    if (UINTPTR_MAX != UINT32_MAX) {
        fprintf(stderr, "rom_mirror: a 64-bit build's code addresses its data relative to itself and cannot run "
                "from a mirror; expansion's script engine needs another way here (rom_mirror.h)\n");
        return ROM_MIRROR_ERR_64_BIT;
    }
    int fd = open(ROM_MIRROR_EXECUTABLE, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "rom_mirror: open %s failed: %s\n", ROM_MIRROR_EXECUTABLE, strerror(errno));
        return ROM_MIRROR_ERR_MAP;
    }
    int result = map_read_only_segments(fd);
    if (close(fd) != 0 && result == ROM_MIRROR_OK) {
        perror("rom_mirror: close");
        result = ROM_MIRROR_ERR_MAP;
    }
    if (result != ROM_MIRROR_OK)
        rom_mirror_shutdown();
    return result;
}

void rom_mirror_shutdown(void)
{
    while (mirror.count > 0) {
        struct mapping *mapping = &mirror.mappings[--mirror.count];
        (void)munmap(mapping->address, mapping->length); /* fails only for a range never mapped */
    }
}
