#include "global.h"
#include "malloc.h"
#if PLATFORM_NATIVE
// GNU ld's default linker script: the executable's first byte, the base of the
// native allocation-site offsets (AllocInternal, MemBlockLocation).
extern const char __executable_start[];
#endif
#line 3
#if TESTING
#include "test/test.h"
#endif

static void *sHeapStart;
static u32 sHeapSize;

ALIGNED(4) EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};

void PutMemBlockHeader(void *block, struct MemBlock *prev, struct MemBlock *next, u32 size)
{
    struct MemBlock *header = (struct MemBlock *)block;

    header->allocated = FALSE;
    header->locationHi = 0;
    header->magic = MALLOC_SYSTEM_ID;
    header->size = size;
    header->locationLo = 0;
    header->prev = prev;
    header->next = next;
}

void PutFirstMemBlockHeader(void *block, u32 size)
{
    PutMemBlockHeader(block, (struct MemBlock *)block, (struct MemBlock *)block, size - sizeof(struct MemBlock));
}

static void *AllocInternal(void *heapStart, u32 size, const char *location)
{
    struct MemBlock *pos = (struct MemBlock *)heapStart;
    struct MemBlock *head = pos;
    struct MemBlock *splitBlock;
    u32 foundBlockSize;

    // Alignment
    if (size & 3)
        size = 4 * ((size / 4) + 1);

    for (;;)
    {
        // Loop through the blocks looking for unused block that's big enough.

        if (!pos->allocated)
        {
            foundBlockSize = pos->size;

            if (foundBlockSize >= size)
            {
                if (foundBlockSize - size < 2 * sizeof(struct MemBlock))
                {
                    // The block isn't much bigger than the requested size,
                    // so just use it.
                    pos->allocated = TRUE;
                }
                else
                {
                    // The block is significantly bigger than the requested
                    // size, so split the rest into a separate block.
                    foundBlockSize -= sizeof(struct MemBlock);
                    foundBlockSize -= size;

                    splitBlock = (struct MemBlock *)(pos->data + size);

                    pos->allocated = TRUE;
                    pos->size = size;

                    PutMemBlockHeader(splitBlock, pos, pos->next, foundBlockSize);

                    pos->next = splitBlock;

                    if (splitBlock->next != head)
                        splitBlock->next->prev = splitBlock;
                }

#if PLATFORM_NATIVE
                // The GBA stores the location string's address, which is in the ROM, without
                // ROM_START's bit (MemBlockLocation puts it back). Natively the executable,
                // strings included, is linked at 0x40000000, so this stores the offset from its
                // first byte instead, in 29 bits: unused_00 holds the top 4. That holds while the
                // executable image is under 512 MiB. A NULL location becomes offset 0, the
                // executable's first byte, as it becomes ROM_START on the GBA.
                {
                    uintptr_t offset = location != NULL ? (uintptr_t)location - (uintptr_t)__executable_start : 0;
                    pos->unused_00 = offset >> 25;
                    pos->locationHi = offset >> 14;
                    pos->locationLo = offset;
                }
#else
#line 77
                pos->locationHi = ((uintptr_t)location) >> 14;
                pos->locationLo = (uintptr_t)location;
#endif
#line 79

                return pos->data;
            }
        }

        if (pos->next == head)
            return NULL;

        pos = pos->next;
    }
}

static void FreeInternal(void *heapStart, void *pointer)
{
    if (pointer)
    {
        struct MemBlock *head = (struct MemBlock *)heapStart;
        struct MemBlock *block = (struct MemBlock *)((u8 *)pointer - sizeof(struct MemBlock));
        AGB_ASSERT(block->magic == MALLOC_SYSTEM_ID);
        AGB_ASSERT(block->allocated == TRUE);
        block->allocated = FALSE;

        // If the freed block isn't the last one, merge with the next block
        // if it's not in use.
        if (block->next != head)
        {
            if (!block->next->allocated)
            {
                block->size += sizeof(struct MemBlock) + block->next->size;
                block->next->magic = 0;
                block->next = block->next->next;
                if (block->next != head)
                    block->next->prev = block;
            }
        }

        // If the freed block isn't the first one, merge with the previous block
        // if it's not in use.
        if (block != head)
        {
            if (!block->prev->allocated)
            {
                AGB_ASSERT(block->prev->magic == MALLOC_SYSTEM_ID);

                block->prev->next = block->next;

                if (block->next != head)
                    block->next->prev = block->prev;

                block->magic = 0;
                block->prev->size += sizeof(struct MemBlock) + block->size;
            }
        }
    }
}

static void *AllocZeroedInternal(void *heapStart, u32 size, const char *location)
{
    void *mem = AllocInternal(heapStart, size, location);

    if (mem != NULL)
    {
        if (size & 3)
            size = 4 * ((size / 4) + 1);

        CpuFill32(0, mem, size);
    }

    return mem;
}

void InitHeap(void *heapStart, u32 heapSize)
{
    sHeapStart = heapStart;
    sHeapSize = heapSize;
    PutFirstMemBlockHeader(heapStart, heapSize);
}

void PrintHeap(void)
{
    const struct MemBlock *head = HeapHead();
    const struct MemBlock *block = head;
    do
    {
        if (block->allocated)
        {
            const char *location = MemBlockLocation(block);
            if (location)
                DebugPrintf("%s: %d bytes allocated", location, block->size);
            else
                DebugPrintf("<unknown>: %d bytes allocated", block->size);
        }
        block = block->next;
    }
    while (block != head);
}

void *Alloc_(u32 size, const char *location)
{
    void *p = AllocInternal(sHeapStart, size, location);
    if (!p)
    {
        if (TESTING)
            PrintHeap();
        fatalf("%s: out of memory trying to allocate %d bytes", location, size);
    }
    return p;
}

void *AllocUnchecked_(u32 size, const char *location)
{
    return AllocInternal(sHeapStart, size, location);
}

void *AllocZeroed_(u32 size, const char *location)
{
    void *p = AllocZeroedInternal(sHeapStart, size, location);
    if (!p)
    {
        if (TESTING)
            PrintHeap();
        fatalf("%s: out of memory trying to allocate %d bytes", location, size);
    }
    return p;
}

void *AllocZeroedUnchecked_(u32 size, const char *location)
{
    return AllocZeroedInternal(sHeapStart, size, location);
}

void Free(void *pointer)
{
    FreeInternal(sHeapStart, pointer);
}

const struct MemBlock *HeapHead(void)
{
    return (const struct MemBlock *)sHeapStart;
}

const char *MemBlockLocation(const struct MemBlock *block)
{
    if (!block->allocated)
        return NULL;

#if PLATFORM_NATIVE
    return __executable_start + (((uintptr_t)block->unused_00 << 25) | (block->locationHi << 14) | block->locationLo);
#else
#line 225
    return (const char *)(ROM_START | (block->locationHi << 14) | block->locationLo);
#endif
#line 226
}
