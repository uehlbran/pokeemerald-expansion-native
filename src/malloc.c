#include "global.h"
#include "malloc.h"
#if PLATFORM_NATIVE
#include "platform/game_hooks.h"
#endif
#line 3
#if TESTING
#include "test/test.h"
#endif

static void *sHeapStart;
static u32 sHeapSize;

#if PLATFORM_NATIVE
#if __SIZEOF_POINTER__ != 8
#line 10
ALIGNED(4) EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};
#else
// 8-byte blocks at 64 bits: struct MemBlock holds pointers (24-byte header), so its
// address and every block size must keep 8-byte alignment.
ALIGNED(8) EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};
#endif
#else
#line 10
ALIGNED(4) EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};
#endif
#line 11

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
#if PLATFORM_NATIVE
#if __SIZEOF_POINTER__ != 8
#line 38
    if (size & 3)
        size = 4 * ((size / 4) + 1);
#else
    if (size & 7)
        size = 8 * ((size / 8) + 1);
#endif
#else
#line 38
    if (size & 3)
        size = 4 * ((size / 4) + 1);
#endif
#line 40

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
                // ROM_START's bit (MemBlockLocation puts it back). Natively this stores the offset
                // from PlatformAllocationSiteBase, in 29 bits: unused_00 holds the top 4. A NULL
                // location becomes offset 0, as it becomes ROM_START on the GBA. An offset that does
                // not fit (a string below the base, or an image over 512 MiB) is stored as 0 too,
                // "no location", rather than a wild address for the leak report to read.
                {
                    uintptr_t offset = location != NULL ? (uintptr_t)location - (uintptr_t)PlatformAllocationSiteBase : 0;
                    if (offset >> PLATFORM_ALLOCATION_SITE_OFFSET_BITS)
                        offset = 0;
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
#if PLATFORM_NATIVE
#if __SIZEOF_POINTER__ != 8
#line 141
        if (size & 3)
            size = 4 * ((size / 4) + 1);
#else
        if (size & 7)
            size = 8 * ((size / 8) + 1);
#endif
#else
#line 141
        if (size & 3)
            size = 4 * ((size / 4) + 1);
#endif
#line 143

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
    return PlatformAllocationSiteBase + (((uintptr_t)block->unused_00 << 25) | (block->locationHi << 14) | block->locationLo);
#else
#line 225
    return (const char *)(ROM_START | (block->locationHi << 14) | block->locationLo);
#endif
#line 226
}
