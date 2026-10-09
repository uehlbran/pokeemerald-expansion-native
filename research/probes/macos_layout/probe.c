/*
 * probe.c: probe M0 of research/m8-inventory.md (sections 2.4, 5.3). Does ld64 build
 * and the macOS loader run an x86_64 executable that is a fixed-address image
 * (-no_pie, image at 0x40000000, small __PAGEZERO) with each GBA region as its own
 * zero-filled segment at its GBA address (regions.S, -Wl,-segaddr)? Plain C11 and
 * the Mach and dyld APIs, no SDL, no platform/ code.
 *
 * It prints the header's address, the slide, every LC_SEGMENT_64 of the image and
 * mach_vm_region for each GBA region, writes and reads back the first and last byte
 * of every region, and ends with one "SUMMARY" line. Exit status 0 only if no check
 * failed. Region addresses and sizes: GBATEK "GBA Memory Map"; regions.S.
 */
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KIB UINT64_C(1024)

/* Where the image goes: research/m8-inventory.md 2.4 and gba_regions.ld's header. */
#define EXPECTED_IMAGE_BASE UINT64_C(0x40000000)
#define EXPECTED_PAGEZERO_SIZE UINT64_C(0x1000)
/* The code must end below 0x42000000 (platform/script_tag.h). */
#define CODE_LIMIT UINT64_C(0x42000000)

struct Region {
    const char *segment;
    uint64_t base;
    uint64_t size;
};

static const struct Region REGIONS[] = {
    {"__GBAEWRAM", UINT64_C(0x02000000), 256 * KIB},
    {"__GBAIWRAM", UINT64_C(0x03000000), 32 * KIB},
    {"__GBAIO", UINT64_C(0x04000000), 4 * KIB},
    {"__GBADEBUG", UINT64_C(0x04FFF000), 4 * KIB},
    {"__GBAPAL", UINT64_C(0x05000000), 4 * KIB},
    {"__GBAVRAM", UINT64_C(0x06000000), 96 * KIB},
    {"__GBAOAM", UINT64_C(0x07000000), 4 * KIB},
    {"__GBASRAM", UINT64_C(0x0E000000), 128 * KIB},
};
#define REGION_COUNT (sizeof REGIONS / sizeof REGIONS[0])

#define PATTERN_FIRST UINT8_C(0xA5)
#define PATTERN_LAST UINT8_C(0x5A)

static unsigned g_pass;
static unsigned g_fail;

static unsigned long long ull(uint64_t value) { return (unsigned long long)value; }

static void check(bool ok, const char *what, const char *subject)
{
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
    }
    printf("%s %s: %s\n", ok ? "PASS" : "FAIL", subject, what);
}

/* The segment load command with this name, or NULL. */
static const struct segment_command_64 *find_segment(const struct mach_header_64 *header,
                                                     const char *name)
{
    const uint8_t *cursor = (const uint8_t *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        const struct load_command *command = (const struct load_command *)cursor;
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)cursor;
            if (strncmp(segment->segname, name, sizeof segment->segname) == 0) {
                return segment;
            }
        }
        cursor += command->cmdsize;
    }
    return NULL;
}

static void print_segments(const struct mach_header_64 *header)
{
    const uint8_t *cursor = (const uint8_t *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        const struct load_command *command = (const struct load_command *)cursor;
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)cursor;
            printf("  segment %-16.16s vmaddr=0x%llx vmsize=0x%llx fileoff=0x%llx filesize=0x%llx "
                   "maxprot=%d initprot=%d nsects=%u\n",
                   segment->segname, ull(segment->vmaddr), ull(segment->vmsize),
                   ull(segment->fileoff), ull(segment->filesize), segment->maxprot,
                   segment->initprot, segment->nsects);
        }
        cursor += command->cmdsize;
    }
}

/* mach_vm_region at the address: does a region containing it exist with read+write? */
static bool vm_region_writable(uint64_t address, const char *label)
{
    mach_vm_address_t where = address;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(mach_task_self(), &where, &size, VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &object);
    if (kr != KERN_SUCCESS) {
        printf("  %s: mach_vm_region(0x%llx) failed, kern_return=%d\n", label, ull(address), kr);
        return false;
    }
    printf("  %s: mach_vm_region(0x%llx) -> start=0x%llx size=0x%llx protection=%d max=%d\n", label,
           ull(address), ull(where), ull(size), info.protection, info.max_protection);
    return where <= address && address < where + size &&
           (info.protection & (VM_PROT_READ | VM_PROT_WRITE)) == (VM_PROT_READ | VM_PROT_WRITE);
}

static void probe_region(const struct mach_header_64 *header, const struct Region *r)
{
    printf("region %s: 0x%llx size 0x%llx\n", r->segment, ull(r->base), ull(r->size));
    const struct segment_command_64 *segment = find_segment(header, r->segment);
    check(segment != NULL, "the segment is in the load commands", r->segment);
    if (segment != NULL) {
        check(segment->vmaddr == r->base, "the segment is at its GBA address", r->segment);
        check(segment->vmsize >= r->size, "the segment is at least the region's size", r->segment);
    }
    uint64_t last = r->base + r->size - 1;
    check(vm_region_writable(r->base, "start"), "start is mapped read-write", r->segment);
    check(vm_region_writable(last, "end"), "end is mapped read-write", r->segment);

    volatile uint8_t *first = (volatile uint8_t *)(uintptr_t)r->base;
    volatile uint8_t *end = (volatile uint8_t *)(uintptr_t)last;
    *first = PATTERN_FIRST;
    *end = PATTERN_LAST;
    check(*first == PATTERN_FIRST, "first byte reads back what was written", r->segment);
    check(*end == PATTERN_LAST, "last byte reads back what was written", r->segment);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    uint64_t code = (uintptr_t)&main;
    printf("image header at 0x%llx, slide %lld, cputype 0x%x, filetype %u, flags 0x%x\n",
           ull((uintptr_t)header), (long long)slide, (unsigned)header->cputype, header->filetype,
           header->flags);
    printf("main at 0x%llx\n", ull(code));
    print_segments(header);

    check(slide == 0, "the slide is zero", "image");
    check((header->flags & MH_PIE) == 0, "the executable is not marked PIE", "image");
    check((uintptr_t)header == EXPECTED_IMAGE_BASE, "the header is at 0x40000000", "image");
    check(code >= EXPECTED_IMAGE_BASE && code < CODE_LIMIT, "code is in [0x40000000, 0x42000000)",
          "image");
    const struct segment_command_64 *zero = find_segment(header, SEG_PAGEZERO);
    check(zero != NULL && zero->vmsize == EXPECTED_PAGEZERO_SIZE, "__PAGEZERO is one 4 KiB page",
          "image");

    for (size_t i = 0; i < REGION_COUNT; i++) {
        probe_region(header, &REGIONS[i]);
    }
    printf("SUMMARY pass=%u fail=%u\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
