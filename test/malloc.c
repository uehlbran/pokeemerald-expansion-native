#include "global.h"
#include "malloc.h"
#include "test/test.h"

// Native only: the allocation site's address is stored as an offset from
// PlatformAllocationSiteBase in the 29 bits struct MemBlock has for it, where
// the GBA stores the ROM address without its top bit.
#if PLATFORM_NATIVE

#include "platform/game_hooks.h"

#define MAX_ENCODED_OFFSET ((1u << PLATFORM_ALLOCATION_SITE_OFFSET_BITS) - 1)

static const char *SiteAt(uintptr_t offset)
{
    return (const char *)((uintptr_t)PlatformAllocationSiteBase + offset);
}

static const char *StoredLocationOf(const char *site)
{
    void *memory = AllocUnchecked_(4, site);
    const struct MemBlock *block = (const struct MemBlock *)((const u8 *)memory - offsetof(struct MemBlock, data));
    const char *stored = MemBlockLocation(block);

    Free(memory);
    return stored;
}

TEST("Allocation sites round-trip through the 29-bit offset up to its largest value")
{
    EXPECT_EQ(StoredLocationOf(SiteAt(0)), SiteAt(0));
    EXPECT_EQ(StoredLocationOf(SiteAt(1)), SiteAt(1));
    EXPECT_EQ(StoredLocationOf(SiteAt(1u << 14)), SiteAt(1u << 14));
    EXPECT_EQ(StoredLocationOf(SiteAt(1u << 25)), SiteAt(1u << 25));
    EXPECT_EQ(StoredLocationOf(SiteAt(MAX_ENCODED_OFFSET)), SiteAt(MAX_ENCODED_OFFSET));
}

TEST("An allocation site that does not fit the offset is stored as no location, not a wild address")
{
    EXPECT_EQ(StoredLocationOf(SiteAt(MAX_ENCODED_OFFSET + 1u)), SiteAt(0));
    EXPECT_EQ(StoredLocationOf(SiteAt((uintptr_t)0 - 1u)), SiteAt(0));
    EXPECT_EQ(StoredLocationOf(NULL), SiteAt(0));
}

TEST("The real allocation site strings of the game are within the offset")
{
    // Alloc's own __FILE__ ":" __LINE__ string, as every game allocation passes it.
    const char *site = __FILE__ ":" STR(__LINE__);

    EXPECT_EQ(StoredLocationOf(site), site);
}

#endif
