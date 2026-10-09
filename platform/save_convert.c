/*
 * save_convert.c: moves the game's save between the 32-bit and the 64-bit
 * native build. Interface and what is converted: save_convert.h.
 *
 * Stands in for the cartridge's save data moving between two hosts whose
 * struct layouts differ (on the GBA there is one layout). Written from
 * expansion's own save format (include/save.h, src/save.c: the slots, the
 * sector footer, CalculateChecksum, SAVEBLOCK_CHUNK) and the two layout
 * tables, save_layout32.h and save_layout64.h; design in docs/DECISIONS.md,
 * "Gate (h) is a platform-side save converter".
 *
 * The block is moved by a segment table: rows that name the same members in
 * both layouts, each with its offset, size and repeat stride per width. A
 * row is bytes (copied; the same size at both widths, asserted below), an
 * object event template's script (left zero) or an Enigma Berry description
 * (the address's low bytes, zero-extended or truncated). The rows cover every
 * member of SaveBlock1; what they leave out in the target is padding, zero.
 *
 * This file is compiled with the game's flags (platform/Makefile, as
 * inspect.c is) so that it can pin the running width's table to the
 * struct layouts the game itself was compiled with: an upstream change to
 * SaveBlock1, ObjectEventTemplate, EnigmaBerry or the sector format stops
 * the build at the static assertion that names the table to update. Module
 * state: none.
 */
#include "save_convert.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "flash.h"
#include "save_layout32.h"
#include "save_layout64.h"

#include "global.h"
#include "save.h"

/* The image's footer fields and the Enigma Berry checksum are the game's own
 * u16 and u32, little-endian on the ARM7TDMI; on a little-endian host they
 * have the same bytes, so memcpy reads and writes them as the game does
 * (save_file.c asserts the same). */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the save image's fields are little-endian");

/* ---- the sector format (include/save.h), the same at both widths ---- */

_Static_assert(sizeof(struct SaveSector) == FLASH_SECTOR_SIZE, "a save sector is one flash sector");
_Static_assert(FLASH_SECTOR_COUNT >= NUM_SAVE_SLOTS * NUM_SECTORS_PER_SLOT, "both save slots are on the chip");

/* The footer fields src/save.c reads (SECTOR_SIGNATURE_OFFSET is save.h's). */
#define SECTOR_ID_OFFSET ((uint32_t)offsetof(struct SaveSector, id))
#define SECTOR_CHECKSUM_OFFSET ((uint32_t)offsetof(struct SaveSector, checksum))
#define SAVEBLOCK1_FIRST_SECTOR_ID ((uint32_t)SECTOR_ID_SAVEBLOCK1_START)
/* SaveBlock1's chunks, one per sector id from SECTOR_ID_SAVEBLOCK1_START. */
#define SAVEBLOCK1_CHUNK_COUNT ((uint32_t)(SECTOR_ID_SAVEBLOCK1_END - SECTOR_ID_SAVEBLOCK1_START + 1))
/* The most a reassembled SaveBlock1 can hold, its chunks back to back. */
#define SAVEBLOCK1_CAPACITY (SAVEBLOCK1_CHUNK_COUNT * (uint32_t)SECTOR_DATA_SIZE)

/* src/save.c CalculateChecksum: the 32-bit sum is folded to 16 bits by
 * adding its upper half to it. */
#define CHECKSUM_FOLD_SHIFT 16u
/* The Enigma Berry checksum and the sector words are u32. */
#define U32_SIZE ((uint32_t)sizeof(uint32_t))

/* ---- the layout tables, pinned in the build of their own width ---- */

#if UINTPTR_MAX > UINT32_MAX
#define NATIVE(name) SAVE_LAYOUT64_##name
#define NATIVE_TABLE "platform/save_layout64.h"
#else
#define NATIVE(name) SAVE_LAYOUT32_##name
#define NATIVE_TABLE "platform/save_layout32.h"
#endif

_Static_assert(sizeof(void *) == NATIVE(POINTER_SIZE), "pointer size: update " NATIVE_TABLE);
_Static_assert(sizeof(struct SaveBlock1) == NATIVE(SAVEBLOCK1_SIZE), "SaveBlock1's size changed: update " NATIVE_TABLE);
_Static_assert(offsetof(struct SaveBlock1, waldaPhrase) + sizeof(struct WaldaPhrase) == NATIVE(SAVEBLOCK1_DATA_END),
               "SaveBlock1's last member moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct SaveBlock1, objectEventTemplates) == NATIVE(OBJECT_EVENT_TEMPLATES),
               "SaveBlock1.objectEventTemplates moved: update " NATIVE_TABLE);
_Static_assert(OBJECT_EVENT_TEMPLATES_COUNT == NATIVE(OBJECT_EVENT_TEMPLATE_COUNT),
               "OBJECT_EVENT_TEMPLATES_COUNT changed: update " NATIVE_TABLE);
_Static_assert(sizeof(struct ObjectEventTemplate) == NATIVE(OBJECT_EVENT_TEMPLATE_SIZE),
               "ObjectEventTemplate's size changed: update " NATIVE_TABLE);
_Static_assert(offsetof(struct ObjectEventTemplate, script) == NATIVE(TEMPLATE_SCRIPT),
               "ObjectEventTemplate.script moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct ObjectEventTemplate, flagId) == NATIVE(TEMPLATE_FLAG_ID),
               "ObjectEventTemplate.flagId moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct SaveBlock1, flags) == NATIVE(FLAGS), "SaveBlock1.flags moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct SaveBlock1, enigmaBerry) == NATIVE(ENIGMA_BERRY),
               "SaveBlock1.enigmaBerry moved: update " NATIVE_TABLE);
_Static_assert(sizeof(struct EnigmaBerry) == NATIVE(ENIGMA_BERRY_SIZE),
               "EnigmaBerry's size changed: update " NATIVE_TABLE);
_Static_assert(offsetof(struct EnigmaBerry, berry.maxYield) == NATIVE(BERRY_MAX_YIELD),
               "EnigmaBerryInfo.maxYield moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct EnigmaBerry, berry.description1) == NATIVE(BERRY_DESCRIPTION1),
               "EnigmaBerryInfo.description1 moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct EnigmaBerry, berry.description2) == NATIVE(BERRY_DESCRIPTION2),
               "EnigmaBerryInfo.description2 moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct EnigmaBerry, berry.growthDuration) == NATIVE(BERRY_GROWTH_DURATION),
               "EnigmaBerryInfo.growthDuration moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct EnigmaBerry, checksum) == NATIVE(ENIGMA_CHECKSUM),
               "EnigmaBerry.checksum moved: update " NATIVE_TABLE);
_Static_assert(offsetof(struct SaveBlock1, mysteryGift) == NATIVE(MYSTERY_GIFT),
               "SaveBlock1.mysteryGift moved: update " NATIVE_TABLE);

/* What the segment table relies on, between the two tables: every bytes row
 * has the same size at both widths, and the members around the pointers are
 * contiguous. */
_Static_assert(SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATES == SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATES, "pos .. objectEvents");
_Static_assert(SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_COUNT == SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_COUNT, "templates");
_Static_assert(SAVE_LAYOUT32_TEMPLATE_SCRIPT == SAVE_LAYOUT64_TEMPLATE_SCRIPT, "localId .. trainerRange");
_Static_assert(SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_SIZE - SAVE_LAYOUT32_TEMPLATE_FLAG_ID
                   == SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_SIZE - SAVE_LAYOUT64_TEMPLATE_FLAG_ID,
               "flagId, filler");
_Static_assert(SAVE_LAYOUT32_TEMPLATE_FLAG_ID == SAVE_LAYOUT32_TEMPLATE_SCRIPT + SAVE_LAYOUT32_POINTER_SIZE
                   && SAVE_LAYOUT64_TEMPLATE_FLAG_ID == SAVE_LAYOUT64_TEMPLATE_SCRIPT + SAVE_LAYOUT64_POINTER_SIZE,
               "flagId follows script");
_Static_assert(SAVE_LAYOUT32_FLAGS
                       == SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATES
                              + SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_COUNT * SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_SIZE
                   && SAVE_LAYOUT64_FLAGS
                          == SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATES
                                 + SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_COUNT * SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_SIZE,
               "flags follows the templates");
_Static_assert(SAVE_LAYOUT32_ENIGMA_BERRY - SAVE_LAYOUT32_FLAGS == SAVE_LAYOUT64_ENIGMA_BERRY - SAVE_LAYOUT64_FLAGS,
               "flags .. roamer");
_Static_assert(SAVE_LAYOUT32_BERRY_MAX_YIELD == SAVE_LAYOUT64_BERRY_MAX_YIELD, "name .. waterBonus");
_Static_assert(SAVE_LAYOUT32_ENIGMA_CHECKSUM - SAVE_LAYOUT32_BERRY_GROWTH_DURATION
                   == SAVE_LAYOUT64_ENIGMA_CHECKSUM - SAVE_LAYOUT64_BERRY_GROWTH_DURATION,
               "growthDuration .. holdEffectParam");
_Static_assert(SAVE_LAYOUT32_MYSTERY_GIFT == SAVE_LAYOUT32_ENIGMA_BERRY + SAVE_LAYOUT32_ENIGMA_BERRY_SIZE
                   && SAVE_LAYOUT64_MYSTERY_GIFT == SAVE_LAYOUT64_ENIGMA_BERRY + SAVE_LAYOUT64_ENIGMA_BERRY_SIZE,
               "mysteryGift follows enigmaBerry");
_Static_assert(SAVE_LAYOUT32_SAVEBLOCK1_DATA_END - SAVE_LAYOUT32_MYSTERY_GIFT
                   == SAVE_LAYOUT64_SAVEBLOCK1_DATA_END - SAVE_LAYOUT64_MYSTERY_GIFT,
               "mysteryGift .. waldaPhrase");
_Static_assert(SAVE_LAYOUT32_SAVEBLOCK1_SIZE <= SAVEBLOCK1_CAPACITY
                   && SAVE_LAYOUT64_SAVEBLOCK1_SIZE <= SAVEBLOCK1_CAPACITY,
               "SaveBlock1 fits its sectors at both widths");

/* ---- the segment table ---- */

enum layout_index { LAYOUT_32, LAYOUT_64, LAYOUT_COUNT };

/* One width's layout, beyond the segment rows: the block's size (which
 * sets the chunk sizes) and where the Enigma Berry's checksum is. */
struct layout {
    uint32_t pointer_bits;
    uint32_t saveblock1_size;
    uint32_t enigma_berry;
    uint32_t enigma_checksum; /* from enigma_berry */
};

static const struct layout layouts[LAYOUT_COUNT] = {
    [LAYOUT_32] = {32u, SAVE_LAYOUT32_SAVEBLOCK1_SIZE, SAVE_LAYOUT32_ENIGMA_BERRY, SAVE_LAYOUT32_ENIGMA_CHECKSUM},
    [LAYOUT_64] = {64u, SAVE_LAYOUT64_SAVEBLOCK1_SIZE, SAVE_LAYOUT64_ENIGMA_BERRY, SAVE_LAYOUT64_ENIGMA_CHECKSUM},
};

enum segment_kind {
    SEGMENT_BYTES,       /* the same bytes at both widths */
    SEGMENT_SCRIPT,      /* ObjectEventTemplate.script: zero in the target */
    SEGMENT_DESCRIPTION, /* an Enigma Berry description: the address's low bytes */
};

/* Where a row is in one layout: count times, stride bytes apart. */
struct segment_place {
    uint32_t offset;
    uint32_t size;
    uint32_t stride;
};

struct segment {
    enum segment_kind kind;
    uint32_t count;
    struct segment_place at[LAYOUT_COUNT];
};

/* maxYield and the byte of bit-fields after it (minYield, waterBonus) end
 * the run of the Enigma Berry before the first description. */
#define BERRY_HEAD_SIZE (SAVE_LAYOUT32_BERRY_MAX_YIELD + 2u)
#define TEMPLATE_COUNT SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_COUNT

static const struct segment segments[] = {
    /* pos .. objectEvents */
    {SEGMENT_BYTES, 1, {{0, SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATES, 0}, {0, SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATES, 0}}},
    /* each object event template: localId .. trainerRange_berryTreeId, script, flagId and filler */
    {SEGMENT_BYTES,
     TEMPLATE_COUNT,
     {{SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATES, SAVE_LAYOUT32_TEMPLATE_SCRIPT, SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_SIZE},
      {SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATES, SAVE_LAYOUT64_TEMPLATE_SCRIPT, SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_SIZE}}},
    {SEGMENT_SCRIPT,
     TEMPLATE_COUNT,
     {{SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATES + SAVE_LAYOUT32_TEMPLATE_SCRIPT, SAVE_LAYOUT32_POINTER_SIZE,
       SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_SIZE},
      {SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATES + SAVE_LAYOUT64_TEMPLATE_SCRIPT, SAVE_LAYOUT64_POINTER_SIZE,
       SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_SIZE}}},
    {SEGMENT_BYTES,
     TEMPLATE_COUNT,
     {{SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATES + SAVE_LAYOUT32_TEMPLATE_FLAG_ID,
       SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_SIZE - SAVE_LAYOUT32_TEMPLATE_FLAG_ID,
       SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_SIZE},
      {SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATES + SAVE_LAYOUT64_TEMPLATE_FLAG_ID,
       SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_SIZE - SAVE_LAYOUT64_TEMPLATE_FLAG_ID,
       SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_SIZE}}},
    /* flags .. roamer */
    {SEGMENT_BYTES,
     1,
     {{SAVE_LAYOUT32_FLAGS, SAVE_LAYOUT32_ENIGMA_BERRY - SAVE_LAYOUT32_FLAGS, 0},
      {SAVE_LAYOUT64_FLAGS, SAVE_LAYOUT64_ENIGMA_BERRY - SAVE_LAYOUT64_FLAGS, 0}}},
    /* enigmaBerry: name .. waterBonus, description1, description2, growthDuration .. holdEffectParam, checksum */
    {SEGMENT_BYTES,
     1,
     {{SAVE_LAYOUT32_ENIGMA_BERRY, BERRY_HEAD_SIZE, 0}, {SAVE_LAYOUT64_ENIGMA_BERRY, BERRY_HEAD_SIZE, 0}}},
    {SEGMENT_DESCRIPTION,
     1,
     {{SAVE_LAYOUT32_ENIGMA_BERRY + SAVE_LAYOUT32_BERRY_DESCRIPTION1, SAVE_LAYOUT32_POINTER_SIZE, 0},
      {SAVE_LAYOUT64_ENIGMA_BERRY + SAVE_LAYOUT64_BERRY_DESCRIPTION1, SAVE_LAYOUT64_POINTER_SIZE, 0}}},
    {SEGMENT_DESCRIPTION,
     1,
     {{SAVE_LAYOUT32_ENIGMA_BERRY + SAVE_LAYOUT32_BERRY_DESCRIPTION2, SAVE_LAYOUT32_POINTER_SIZE, 0},
      {SAVE_LAYOUT64_ENIGMA_BERRY + SAVE_LAYOUT64_BERRY_DESCRIPTION2, SAVE_LAYOUT64_POINTER_SIZE, 0}}},
    {SEGMENT_BYTES,
     1,
     {{SAVE_LAYOUT32_ENIGMA_BERRY + SAVE_LAYOUT32_BERRY_GROWTH_DURATION,
       SAVE_LAYOUT32_ENIGMA_CHECKSUM - SAVE_LAYOUT32_BERRY_GROWTH_DURATION, 0},
      {SAVE_LAYOUT64_ENIGMA_BERRY + SAVE_LAYOUT64_BERRY_GROWTH_DURATION,
       SAVE_LAYOUT64_ENIGMA_CHECKSUM - SAVE_LAYOUT64_BERRY_GROWTH_DURATION, 0}}},
    {SEGMENT_BYTES,
     1,
     {{SAVE_LAYOUT32_ENIGMA_BERRY + SAVE_LAYOUT32_ENIGMA_CHECKSUM, U32_SIZE, 0},
      {SAVE_LAYOUT64_ENIGMA_BERRY + SAVE_LAYOUT64_ENIGMA_CHECKSUM, U32_SIZE, 0}}},
    /* mysteryGift .. waldaPhrase; at 64 bits the tail padding follows, zero */
    {SEGMENT_BYTES,
     1,
     {{SAVE_LAYOUT32_MYSTERY_GIFT, SAVE_LAYOUT32_SAVEBLOCK1_DATA_END - SAVE_LAYOUT32_MYSTERY_GIFT, 0},
      {SAVE_LAYOUT64_MYSTERY_GIFT, SAVE_LAYOUT64_SAVEBLOCK1_DATA_END - SAVE_LAYOUT64_MYSTERY_GIFT, 0}}},
};
#define SEGMENT_COUNT (sizeof segments / sizeof segments[0])

/* ---- the conversion ---- */

/* The game's u16 and u32 fields in the image, read and written as the
 * ARM7TDMI does: little-endian, at any alignment. */
static uint16_t read_u16(const uint8_t *bytes)
{
    uint16_t value;
    memcpy(&value, bytes, sizeof value);
    return value;
}

static uint32_t read_u32(const uint8_t *bytes)
{
    uint32_t value;
    memcpy(&value, bytes, sizeof value);
    return value;
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    memcpy(bytes, &value, sizeof value);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    memcpy(bytes, &value, sizeof value);
}

/* src/save.c CalculateChecksum, the check of a sector's data: the wrapping
 * u32 sum of its first size / 4 little-endian words, folded to 16 bits. */
static uint16_t sector_checksum(const uint8_t *data, uint32_t size)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < size / U32_SIZE; i++)
        sum += read_u32(data + i * U32_SIZE);
    return (uint16_t)((sum >> CHECKSUM_FOLD_SHIFT) + sum);
}

/* src/berry.c GetEnigmaBerryChecksum: the u32 sum of the record's bytes
 * before its checksum, padding and pointer bytes included. */
static uint32_t enigma_berry_sum(const uint8_t *saveblock1, const struct layout *layout)
{
    const uint8_t *record = saveblock1 + layout->enigma_berry;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < layout->enigma_checksum; i++)
        sum += record[i];
    return sum;
}

/* src/save.c SAVEBLOCK_CHUNK: the bytes of SaveBlock1 in its chunk-th
 * sector, SECTOR_DATA_SIZE but for the last. */
static uint32_t chunk_size(const struct layout *layout, uint32_t chunk)
{
    const uint32_t start = chunk * (uint32_t)SECTOR_DATA_SIZE;
    if (layout->saveblock1_size <= start)
        return 0;
    const uint32_t rest = layout->saveblock1_size - start;
    return rest < (uint32_t)SECTOR_DATA_SIZE ? rest : (uint32_t)SECTOR_DATA_SIZE;
}

/* Where flash sector n is in the chip's image (flash.h: n * FLASH_SECTOR_SIZE). */
static size_t sector_offset(uint32_t sector)
{
    return (size_t)sector * FLASH_SECTOR_SIZE;
}

/* What src/save.c GetSaveValidStatus accepts of SaveBlock1's sectors in a
 * slot written by the game: each chunk in one sector, with the signature and
 * a checksum that matches at the layout's chunk size. Fills sector_of_chunk
 * and returns true if so; a chunk in two sectors is not a slot the game
 * wrote, and returns false. */
static bool find_saveblock1(const uint8_t *image, uint32_t slot, const struct layout *layout,
                            uint32_t sector_of_chunk[SAVEBLOCK1_CHUNK_COUNT])
{
    bool found[SAVEBLOCK1_CHUNK_COUNT] = {false};
    for (uint32_t i = 0; i < NUM_SECTORS_PER_SLOT; i++) {
        const uint32_t sector = slot * NUM_SECTORS_PER_SLOT + i;
        const uint8_t *bytes = image + sector_offset(sector);
        if (read_u32(bytes + (uint32_t)SECTOR_SIGNATURE_OFFSET) != SECTOR_SIGNATURE)
            continue;
        /* an id below SaveBlock1's wraps past the chunk count too */
        const uint32_t chunk = (uint32_t)read_u16(bytes + SECTOR_ID_OFFSET) - SAVEBLOCK1_FIRST_SECTOR_ID;
        if (chunk >= SAVEBLOCK1_CHUNK_COUNT)
            continue;
        if (found[chunk]
            || read_u16(bytes + SECTOR_CHECKSUM_OFFSET) != sector_checksum(bytes, chunk_size(layout, chunk)))
            return false;
        found[chunk] = true;
        sector_of_chunk[chunk] = sector;
    }
    for (uint32_t chunk = 0; chunk < SAVEBLOCK1_CHUNK_COUNT; chunk++) {
        if (!found[chunk])
            return false;
    }
    return true;
}

/* Moves SaveBlock1 from one layout to the other, row by row; target starts
 * zeroed. Then the Enigma Berry's checksum, if it held in the source, is
 * made to hold in the target, as GetEnigmaBerryChecksum will compute it. */
static void convert_saveblock1(const uint8_t *source, enum layout_index from, uint8_t *target, enum layout_index to)
{
    for (size_t row = 0; row < SEGMENT_COUNT; row++) {
        const struct segment *segment = &segments[row];
        const struct segment_place *in = &segment->at[from];
        const struct segment_place *out = &segment->at[to];
        for (size_t i = 0; i < segment->count; i++) {
            const uint8_t *src = source + in->offset + i * in->stride;
            uint8_t *dst = target + out->offset + i * out->stride;
            switch (segment->kind) {
            case SEGMENT_BYTES:
                memcpy(dst, src, out->size);
                break;
            case SEGMENT_SCRIPT:
                break; /* zero: LoadSaveblockObjEventScripts refills it */
            case SEGMENT_DESCRIPTION:
                /* little-endian: the low bytes come first */
                memcpy(dst, src, in->size < out->size ? in->size : out->size);
                break;
            }
        }
    }
    const struct layout *in_layout = &layouts[from];
    const struct layout *out_layout = &layouts[to];
    if (enigma_berry_sum(source, in_layout) == read_u32(source + in_layout->enigma_berry + in_layout->enigma_checksum))
        write_u32(target + out_layout->enigma_berry + out_layout->enigma_checksum,
                  enigma_berry_sum(target, out_layout));
}

/* One save slot: SaveBlock1's sectors rewritten in the target layout, or
 * nothing if the slot's SaveBlock1 is not intact in the source layout.
 * Returns whether it converted. */
static bool convert_slot(uint8_t *image, uint32_t slot, enum layout_index from, enum layout_index to)
{
    uint32_t sector_of_chunk[SAVEBLOCK1_CHUNK_COUNT];
    if (!find_saveblock1(image, slot, &layouts[from], sector_of_chunk))
        return false;

    uint8_t source[SAVEBLOCK1_CAPACITY] = {0};
    uint8_t target[SAVEBLOCK1_CAPACITY] = {0};
    for (uint32_t chunk = 0; chunk < SAVEBLOCK1_CHUNK_COUNT; chunk++)
        memcpy(source + (size_t)chunk * SECTOR_DATA_SIZE, image + sector_offset(sector_of_chunk[chunk]),
               chunk_size(&layouts[from], chunk));
    convert_saveblock1(source, from, target, to);
    for (uint32_t chunk = 0; chunk < SAVEBLOCK1_CHUNK_COUNT; chunk++) {
        /* HandleWriteSector: the data, zeros to the end of the data area,
         * the checksum of the data; id, signature, counter and the
         * SaveBlock3 chunk stay. */
        uint8_t *bytes = image + sector_offset(sector_of_chunk[chunk]);
        const uint32_t size = chunk_size(&layouts[to], chunk);
        memcpy(bytes, target + (size_t)chunk * SECTOR_DATA_SIZE, size);
        memset(bytes + size, 0, (uint32_t)SECTOR_DATA_SIZE - size);
        write_u16(bytes + SECTOR_CHECKSUM_OFFSET, sector_checksum(bytes, size));
    }
    return true;
}

/* The layout of a build with pointer_bits-bit pointers, if one is known. */
static bool layout_for_bits(uint32_t pointer_bits, enum layout_index *out)
{
    for (int i = 0; i < LAYOUT_COUNT; i++) {
        if (layouts[i].pointer_bits == pointer_bits) {
            *out = (enum layout_index)i;
            return true;
        }
    }
    return false;
}

int save_convert_image(uint8_t *image, uint32_t from_bits, uint32_t to_bits)
{
    enum layout_index from;
    enum layout_index to;
    if (image == NULL || !layout_for_bits(from_bits, &from) || !layout_for_bits(to_bits, &to) || from == to)
        return SAVE_CONVERT_ERR_ARGUMENT;
    int converted = 0;
    for (uint32_t slot = 0; slot < NUM_SAVE_SLOTS; slot++) {
        if (convert_slot(image, slot, from, to))
            converted++;
    }
    return converted;
}
