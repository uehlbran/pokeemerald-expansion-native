/*
 * Tests of save_convert.c: a 32-bit build's save moved to the 64-bit layout
 * and back. The expectations are written out here, independently of the
 * module's tables: the sector format from include/save.h and src/save.c
 * (HandleWriteSector, CalculateChecksum, SAVEBLOCK_CHUNK), SaveBlock1's
 * members at both widths from the milestone 7 measurement recorded in
 * docs/DECISIONS.md ("Gate (h) is a platform-side save converter") and
 * platform/save_layout32.h and 64.h, the Enigma Berry checksum from
 * src/berry.c GetEnigmaBerryChecksum.
 *
 * The synthetic saves are what the game writes: two slots of 14 sectors,
 * the sector ids rotated within each slot, every sector with its id, the
 * signature, the save counter and a checksum over its chunk. Each byte of
 * a SaveBlock1 is a function of its offset and its slot (never 0), so a byte
 * moved to the wrong place, or one left in a place that must be zero, shows.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "flash.h"
#include "ptest.h"
#include "save_convert.h"

/* include/save.h */
#define SECTOR_BYTES 4096u
#define SECTOR_DATA 3968u
#define SB3_CHUNK_AT 3968u
#define SB3_CHUNK_BYTES 116u
#define ID_AT 4084u
#define CHECKSUM_AT 4086u
#define SIGNATURE_AT 4088u
#define COUNTER_AT 4092u
#define SIGNATURE 0x08012025u
#define SECTORS_PER_SLOT 14u
#define SLOTS 2u
#define CHIP_BYTES (32u * SECTOR_BYTES)
#define SB1_FIRST_ID 1u
#define SB1_CHUNKS 4u
#define SB1_CAPACITY (SB1_CHUNKS * SECTOR_DATA)

/* sizeof(struct SaveBlock2) and sizeof(struct PokemonStorage), the same at
 * both widths, for the other sectors' checksums. */
#define SB2_BYTES 3848u
#define STORAGE_BYTES 34144u
#define STORAGE_FIRST_ID 5u

/* SaveBlock1 at 32 and 64 bits. */
#define SB1_32 15496u
#define SB1_64 15768u
#define TEMPLATES 3144u
#define TEMPLATE_COUNT 64u
#define TEMPLATE_32 24u
#define TEMPLATE_64 28u
#define FLAGS_32 4680u
#define FLAGS_64 4936u
#define ENIGMA_32 12720u
#define ENIGMA_64 12976u
#define ENIGMA_SUMMED_32 48u /* the bytes before the checksum */
#define ENIGMA_SUMMED_64 60u
#define MYSTERY_GIFT_32 12772u
#define MYSTERY_GIFT_64 13040u
#define DATA_END_64 15764u /* then 4 bytes of tail padding */

_Static_assert(FLASH_CHIP_SIZE == CHIP_BYTES, "chip size");

static uint8_t image[CHIP_BYTES];
static uint8_t original[CHIP_BYTES];

/* ---- building saves as src/save.c writes them ---- */

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        bytes[i] = (uint8_t)(value >> (8 * i));
}

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

/* CalculateChecksum. */
static uint16_t calculate_checksum(const uint8_t *data, uint32_t size)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size / 4; i++)
        sum += get_u32(data + 4 * i);
    return (uint16_t)((sum >> 16) + sum);
}

/* Never 0, different for every offset nearby and for the other slot. */
static uint8_t pattern(uint32_t salt, uint32_t offset)
{
    return (uint8_t)(1 + (offset * 37u + (offset >> 8) * 101u + salt * 59u) % 255u);
}

/* SAVEBLOCK_CHUNK: the bytes of a structure of size bytes in its chunk-th sector. */
static uint32_t chunk_size(uint32_t size, uint32_t chunk)
{
    const uint32_t start = chunk * SECTOR_DATA;
    if (size <= start)
        return 0;
    return size - start < SECTOR_DATA ? size - start : SECTOR_DATA;
}

static uint8_t *sector(uint32_t slot, uint32_t position)
{
    return image + (slot * SECTORS_PER_SLOT + position) * SECTOR_BYTES;
}

/* Where HandleWriteSector puts sector id when the slot was last written with
 * gLastWrittenSector = rotation. */
static uint8_t *sector_of_id(uint32_t slot, uint32_t rotation, uint32_t id)
{
    return sector(slot, (id + rotation) % SECTORS_PER_SLOT);
}

static uint32_t size_of_id(uint32_t id, uint32_t sb1_size)
{
    if (id == 0)
        return SB2_BYTES;
    if (id < STORAGE_FIRST_ID)
        return chunk_size(sb1_size, id - SB1_FIRST_ID);
    return chunk_size(STORAGE_BYTES, id - STORAGE_FIRST_ID);
}

/* One slot as the game writes it, with SaveBlock1's sb1_size bytes from
 * sb1 and every other sector's data and every SaveBlock3 chunk a pattern. */
static void write_slot(uint32_t slot, uint32_t rotation, uint32_t counter, const uint8_t *sb1, uint32_t sb1_size)
{
    for (uint32_t id = 0; id < SECTORS_PER_SLOT; id++) {
        uint8_t *bytes = sector_of_id(slot, rotation, id);
        memset(bytes, 0, SECTOR_BYTES);
        const uint32_t size = size_of_id(id, sb1_size);
        for (uint32_t i = 0; i < size; i++) {
            const bool saveblock1 = id >= SB1_FIRST_ID && id < SB1_FIRST_ID + SB1_CHUNKS;
            bytes[i] = saveblock1 ? sb1[(id - SB1_FIRST_ID) * SECTOR_DATA + i] : pattern(slot + 7 * id, i);
        }
        for (uint32_t i = 0; i < SB3_CHUNK_BYTES; i++)
            bytes[SB3_CHUNK_AT + i] = pattern(slot + 100 + id, i);
        put_u16(bytes + ID_AT, (uint16_t)id);
        put_u16(bytes + CHECKSUM_AT, calculate_checksum(bytes, size));
        put_u32(bytes + SIGNATURE_AT, SIGNATURE);
        put_u32(bytes + COUNTER_AT, counter);
    }
}

/* src/berry.c GetEnigmaBerryChecksum over the record at record, summed bytes long. */
static uint32_t enigma_sum(const uint8_t *record, uint32_t summed)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < summed; i++)
        sum += record[i];
    return sum;
}

/* A 32-bit SaveBlock1 of pattern bytes whose Enigma Berry checksum holds or not. */
static void make_saveblock1_32(uint8_t block[SB1_CAPACITY], uint32_t salt, bool enigma_valid)
{
    memset(block, 0, SB1_CAPACITY);
    for (uint32_t i = 0; i < SB1_32; i++)
        block[i] = pattern(salt, i);
    const uint32_t sum = enigma_sum(block + ENIGMA_32, ENIGMA_SUMMED_32);
    put_u32(block + ENIGMA_32 + ENIGMA_SUMMED_32, enigma_valid ? sum : sum + 1);
}

/* The rotations: slot 1's SaveBlock1 wraps past the end of the slot (ids 1-4
 * at positions 12, 13, 0, 1). */
#define ROTATION_0 5u
#define ROTATION_1 11u
#define COUNTER_0 2u
#define COUNTER_1 1u

static uint8_t block0[SB1_CAPACITY];
static uint8_t block1[SB1_CAPACITY];

/* Both slots intact; slot 0's Enigma Berry valid, slot 1's not. The
 * special sectors (28-31) hold a pattern. */
static void make_image_32(void)
{
    memset(image, 0xFF, sizeof image);
    make_saveblock1_32(block0, 0, true);
    make_saveblock1_32(block1, 1, false);
    write_slot(0, ROTATION_0, COUNTER_0, block0, SB1_32);
    write_slot(1, ROTATION_1, COUNTER_1, block1, SB1_32);
    for (uint32_t i = SLOTS * SECTORS_PER_SLOT * SECTOR_BYTES; i < CHIP_BYTES; i++)
        image[i] = pattern(9, i);
    memcpy(original, image, sizeof image);
}

/* ---- what the 64-bit block must hold ---- */

#define MUST_BE_ZERO UINT32_MAX

/* The 32-bit offset of the byte the 64-bit block holds at offset, or
 * MUST_BE_ZERO: a template's script, the hole before description1, a
 * description's upper half, the tail padding. */
static uint32_t source_of_64(uint32_t offset)
{
    if (offset < TEMPLATES)
        return offset;
    if (offset < FLAGS_64) {
        const uint32_t template = (offset - TEMPLATES) / TEMPLATE_64;
        const uint32_t at = (offset - TEMPLATES) % TEMPLATE_64;
        const uint32_t base = TEMPLATES + template * TEMPLATE_32;
        if (at < 16)
            return base + at; /* localId .. trainerRange_berryTreeId */
        if (at < 24)
            return MUST_BE_ZERO;      /* script */
        return base + 20 + (at - 24); /* flagId, filler */
    }
    if (offset < ENIGMA_64)
        return offset - (FLAGS_64 - FLAGS_32);
    if (offset < MYSTERY_GIFT_64) {
        const uint32_t at = offset - ENIGMA_64;
        if (at < 12)
            return ENIGMA_32 + at; /* name .. waterBonus */
        if (at < 16)
            return MUST_BE_ZERO; /* the hole */
        if (at < 20)
            return ENIGMA_32 + 12 + (at - 16); /* description1, low half */
        if (at < 24)
            return MUST_BE_ZERO;
        if (at < 28)
            return ENIGMA_32 + 16 + (at - 24); /* description2, low half */
        if (at < 32)
            return MUST_BE_ZERO;
        return ENIGMA_32 + 20 + (at - 32); /* growthDuration .. holdEffectParam, checksum */
    }
    if (offset < DATA_END_64)
        return offset - (MYSTERY_GIFT_64 - MYSTERY_GIFT_32);
    return MUST_BE_ZERO; /* tail padding */
}

/* The 64-bit block expected from a 32-bit one. */
static void expected_64(const uint8_t *block32, uint8_t expected[SB1_CAPACITY])
{
    memset(expected, 0, SB1_CAPACITY);
    for (uint32_t i = 0; i < SB1_64; i++) {
        const uint32_t source = source_of_64(i);
        expected[i] = source == MUST_BE_ZERO ? 0 : block32[source];
    }
}

/* Slot slot of image holds block (sb1_size bytes) as the game would have
 * written it, and every other sector and every footer field but SaveBlock1's
 * checksums is original's. */
static void check_slot(uint32_t slot, uint32_t rotation, const uint8_t *block, uint32_t sb1_size)
{
    for (uint32_t id = 0; id < SECTORS_PER_SLOT; id++) {
        const uint8_t *bytes = sector_of_id(slot, rotation, id);
        const uint8_t *before = original + (bytes - image);
        const bool saveblock1 = id >= SB1_FIRST_ID && id < SB1_FIRST_ID + SB1_CHUNKS;
        if (!saveblock1) {
            PT_ASSERT_MEM_EQ(bytes, before, SECTOR_BYTES);
            continue;
        }
        const uint32_t size = chunk_size(sb1_size, id - SB1_FIRST_ID);
        PT_ASSERT_MEM_EQ(bytes, block + (id - SB1_FIRST_ID) * SECTOR_DATA, size);
        for (uint32_t i = size; i < SECTOR_DATA; i++)
            PT_ASSERT_EQ_U32(bytes[i], 0);
        PT_ASSERT_MEM_EQ(bytes + SB3_CHUNK_AT, before + SB3_CHUNK_AT, SB3_CHUNK_BYTES);
        PT_ASSERT_EQ_U32(get_u16(bytes + ID_AT), get_u16(before + ID_AT));
        PT_ASSERT_EQ_U32(get_u16(bytes + CHECKSUM_AT), calculate_checksum(bytes, size));
        PT_ASSERT_EQ_U32(get_u32(bytes + SIGNATURE_AT), get_u32(before + SIGNATURE_AT));
        PT_ASSERT_EQ_U32(get_u32(bytes + COUNTER_AT), get_u32(before + COUNTER_AT));
    }
}

static void check_special_sectors_untouched(void)
{
    const uint32_t from = SLOTS * SECTORS_PER_SLOT * SECTOR_BYTES;
    PT_ASSERT_MEM_EQ(image + from, original + from, CHIP_BYTES - from);
}

static void check_slot_untouched(uint32_t slot)
{
    const uint32_t from = slot * SECTORS_PER_SLOT * SECTOR_BYTES;
    PT_ASSERT_MEM_EQ(image + from, original + from, SECTORS_PER_SLOT * SECTOR_BYTES);
}

/* ---- 32 to 64 ---- */

PTEST(both_slots_move_to_the_64_bit_layout)
{
    static uint8_t expected0[SB1_CAPACITY];
    static uint8_t expected1[SB1_CAPACITY];
    make_image_32();
    expected_64(block0, expected0);
    expected_64(block1, expected1);

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    check_slot(0, ROTATION_0, expected0, SB1_64);
    check_slot(1, ROTATION_1, expected1, SB1_64);
    check_special_sectors_untouched();
}

PTEST(the_last_chunk_grows_and_its_checksum_covers_it)
{
    make_image_32();
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    /* id 4 holds 15768 - 3 * 3968 = 3864 bytes at 64 bits, 3592 at 32; its
     * last 4 bytes are the tail padding, the 268 before them the block's
     * last members, shifted. */
    const uint8_t *last = sector_of_id(0, ROTATION_0, 4);
    PT_ASSERT_EQ_U32(chunk_size(SB1_64, 3), 3864);
    PT_ASSERT_EQ_U32(last[3859], block0[SB1_32 - 1]);
    PT_ASSERT_EQ_U32(last[3860], 0);
    PT_ASSERT_EQ_U32(get_u16(last + CHECKSUM_AT), calculate_checksum(last, 3864));
    PT_ASSERT(get_u16(last + CHECKSUM_AT) != calculate_checksum(last, 3592));
}

PTEST(the_enigma_berry_checksum_holds_after_the_move_only_if_it_held_before)
{
    static uint8_t expected0[SB1_CAPACITY];
    make_image_32();
    expected_64(block0, expected0);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    /* Reassemble each slot's record: it is in chunk 3 (12976 / 3968 = 3,
     * offset 1072). */
    const uint8_t *record0 = sector_of_id(0, ROTATION_0, 4) + (ENIGMA_64 - 3 * SECTOR_DATA);
    const uint8_t *record1 = sector_of_id(1, ROTATION_1, 4) + (ENIGMA_64 - 3 * SECTOR_DATA);
    PT_ASSERT_EQ_U32(get_u32(record0 + ENIGMA_SUMMED_64), enigma_sum(record0, ENIGMA_SUMMED_64));
    PT_ASSERT(get_u32(record1 + ENIGMA_SUMMED_64) != enigma_sum(record1, ENIGMA_SUMMED_64));
    /* The 32-bit invalid checksum is carried over as it was. */
    PT_ASSERT_EQ_U32(get_u32(record1 + ENIGMA_SUMMED_64), get_u32(block1 + ENIGMA_32 + ENIGMA_SUMMED_32));
    /* The descriptions are zero-extended: low half the 32-bit address, high half zero. */
    PT_ASSERT_MEM_EQ(record0 + 16, block0 + ENIGMA_32 + 12, 4);
    PT_ASSERT_EQ_U32(get_u32(record0 + 20), 0);
    PT_ASSERT_MEM_EQ(record0 + 24, block0 + ENIGMA_32 + 16, 4);
    PT_ASSERT_EQ_U32(get_u32(record0 + 28), 0);
    PT_ASSERT_MEM_EQ(record0, expected0 + ENIGMA_64, 64);
}

PTEST(a_slot_with_a_damaged_saveblock1_sector_is_left_alone)
{
    static uint8_t expected1[SB1_CAPACITY];
    make_image_32();
    expected_64(block1, expected1);
    /* Slot 0's id 3 no longer matches its checksum (a torn write). */
    sector_of_id(0, ROTATION_0, 3)[100] ^= 0x01;
    memcpy(original, image, sizeof image);

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 1);
    check_slot_untouched(0);
    check_slot(1, ROTATION_1, expected1, SB1_64);
    check_special_sectors_untouched();
}

PTEST(a_slot_missing_a_saveblock1_signature_is_left_alone)
{
    make_image_32();
    put_u32(sector_of_id(1, ROTATION_1, 1) + SIGNATURE_AT, 0);
    memcpy(original, image, sizeof image);

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 1);
    check_slot_untouched(1);
}

PTEST(a_slot_with_two_sectors_for_one_saveblock1_chunk_is_left_alone)
{
    make_image_32();
    /* id 9 overwritten with a valid copy of id 1: every chunk is there, the
     * first twice. Which one the game would read is not defined (and it
     * rejects the slot, which lacks id 9). */
    memcpy(sector_of_id(0, ROTATION_0, 9), sector_of_id(0, ROTATION_0, 1), SECTOR_BYTES);
    memcpy(original, image, sizeof image);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 1);
    check_slot_untouched(0);

    /* id 2 overwritten instead: chunk 1 twice, chunk 2 missing. */
    make_image_32();
    memcpy(sector_of_id(1, ROTATION_1, 2), sector_of_id(1, ROTATION_1, 1), SECTOR_BYTES);
    memcpy(original, image, sizeof image);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 1);
    check_slot_untouched(1);
}

PTEST(a_stale_saveblock1_sector_with_a_bad_checksum_is_ignored)
{
    /* id 9's sector holds an older, torn copy of id 2: the signature, id 2,
     * a checksum that does not match. The game counts only the valid id 2
     * (GetSaveValidStatus, CopySaveSlotData); so does the converter, and the
     * stale sector stays as it was. */
    static uint8_t expected0[SB1_CAPACITY];
    make_image_32();
    expected_64(block0, expected0);
    uint8_t *stale = sector_of_id(0, ROTATION_0, 9);
    memcpy(stale, sector_of_id(0, ROTATION_0, 2), SECTOR_BYTES);
    stale[200] ^= 0x10;
    memcpy(original, image, sizeof image);

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    check_slot(0, ROTATION_0, expected0, SB1_64); /* id 9's position compared whole with the original */
    PT_ASSERT_EQ_U32(get_u16(stale + ID_AT), 2);
    PT_ASSERT(get_u16(stale + CHECKSUM_AT) != calculate_checksum(stale, SECTOR_DATA));
}

PTEST(a_damaged_sector_outside_saveblock1_does_not_stop_the_slot)
{
    /* Every sector keeps the validity it had: GetSaveValidStatus rejects the
     * slot at 64 bits as it did at 32, whatever SaveBlock1 holds. */
    static uint8_t expected0[SB1_CAPACITY];
    make_image_32();
    expected_64(block0, expected0);
    sector_of_id(0, ROTATION_0, 7)[5] ^= 0x80;
    memcpy(original, image, sizeof image);

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    check_slot(0, ROTATION_0, expected0, SB1_64);
    const uint8_t *damaged = sector_of_id(0, ROTATION_0, 7);
    PT_ASSERT(get_u16(damaged + CHECKSUM_AT) != calculate_checksum(damaged, SECTOR_DATA));
}

PTEST(an_erased_chip_and_an_empty_slot_are_left_alone)
{
    memset(image, 0xFF, sizeof image);
    memcpy(original, image, sizeof image);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 0);
    PT_ASSERT_MEM_EQ(image, original, CHIP_BYTES);

    /* A first save: slot 1 written, slot 0 still erased. */
    make_saveblock1_32(block1, 1, true);
    write_slot(1, 0, 1, block1, SB1_32);
    memcpy(original, image, sizeof image);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 1);
    check_slot_untouched(0);
}

PTEST(a_64_bit_save_is_not_taken_for_a_32_bit_one)
{
    /* Converting twice: the second pass finds id 4's checksum wrong at 32-bit sizes. */
    make_image_32();
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    memcpy(original, image, sizeof image);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 0);
    PT_ASSERT_MEM_EQ(image, original, CHIP_BYTES);
}

PTEST(the_save_counter_does_not_decide_which_slot_is_converted)
{
    /* Both slots intact: the converter moves both, whichever the game would
     * load (the higher counter), and leaves each counter as it was. */
    static const uint32_t counters[][2] = {{1, 2}, {5, 5}, {0, UINT32_MAX}, {UINT32_MAX, 0}};
    static uint8_t expected0[SB1_CAPACITY];
    static uint8_t expected1[SB1_CAPACITY];
    for (size_t i = 0; i < sizeof counters / sizeof counters[0]; i++) {
        make_image_32();
        write_slot(0, ROTATION_0, counters[i][0], block0, SB1_32);
        write_slot(1, ROTATION_1, counters[i][1], block1, SB1_32);
        memcpy(original, image, sizeof image);
        expected_64(block0, expected0);
        expected_64(block1, expected1);

        PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
        check_slot(0, ROTATION_0, expected0, SB1_64);
        check_slot(1, ROTATION_1, expected1, SB1_64);
        check_special_sectors_untouched();
    }
}

PTEST(saveblock1_in_the_last_sector_of_the_slot_and_of_the_save_area_is_converted)
{
    /* Slot 1 rotated so that id 4 is at position 13, flash sector 27, the
     * last one the game's slots use; slot 0 so that id 1 is at its position
     * 13 (sector 13) and the others wrap to the start. The sectors after
     * (28 to 31) are the special ones and stay. */
    static uint8_t expected0[SB1_CAPACITY];
    static uint8_t expected1[SB1_CAPACITY];
    const uint32_t rotation_0 = 12;
    const uint32_t rotation_1 = 9;
    make_image_32();
    write_slot(0, rotation_0, COUNTER_0, block0, SB1_32);
    write_slot(1, rotation_1, COUNTER_1, block1, SB1_32);
    memcpy(original, image, sizeof image);
    expected_64(block0, expected0);
    expected_64(block1, expected1);
    PT_ASSERT(sector_of_id(1, rotation_1, 4) == image + 27 * SECTOR_BYTES);

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    check_slot(0, rotation_0, expected0, SB1_64);
    check_slot(1, rotation_1, expected1, SB1_64);
    check_special_sectors_untouched();
}

PTEST(valid_saveblock1_sectors_beyond_the_two_slots_are_not_read_or_changed)
{
    /* Sectors 28 to 31 hold a valid-looking id 1 to 4 each (as a Hall of
     * Fame or a recorded battle never would): the converter reads the two
     * slots only, so these neither count as a second copy nor change. */
    make_image_32();
    for (uint32_t id = SB1_FIRST_ID; id < SB1_FIRST_ID + SB1_CHUNKS; id++)
        memcpy(image + (SLOTS * SECTORS_PER_SLOT + id - SB1_FIRST_ID) * SECTOR_BYTES, sector_of_id(0, ROTATION_0, id),
               SECTOR_BYTES);
    memcpy(original, image, sizeof image);

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    check_special_sectors_untouched();
}

PTEST(sector_ids_at_the_edges_outside_saveblock1_are_not_chunks)
{
    /* id 9's sector says id 5 (one past the last chunk; checksum 0, which is
     * right for the zero bytes of chunk 4), id 0 (one below the first: the
     * unsigned wrap) or id 0xFFFF, each with the signature: not SaveBlock1's,
     * so the slot still converts and that sector stays as it is. */
    static const uint16_t ids[] = {SB1_FIRST_ID + SB1_CHUNKS, SB1_FIRST_ID - 1, UINT16_MAX};
    static uint8_t expected0[SB1_CAPACITY];
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        make_image_32();
        expected_64(block0, expected0);
        uint8_t *other = sector_of_id(0, ROTATION_0, 9);
        put_u16(other + ID_AT, ids[i]);
        put_u16(other + CHECKSUM_AT, 0);
        memcpy(original, image, sizeof image);

        PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
        check_slot(0, ROTATION_0, expected0, SB1_64);
        PT_ASSERT_EQ_U32(get_u16(other + ID_AT), ids[i]);
    }
}

/* The 32-bit block the converter makes from block64 read as a 64-bit one:
 * every byte the 64-bit table says came from a 32-bit offset goes back there;
 * the template scripts are zero. */
static void expected_32_from_64(const uint8_t *block64, uint8_t expected[SB1_CAPACITY])
{
    memset(expected, 0, SB1_CAPACITY);
    for (uint32_t i = 0; i < SB1_64; i++) {
        const uint32_t target = source_of_64(i);
        if (target != MUST_BE_ZERO)
            expected[target] = block64[i];
    }
}

PTEST(the_checksums_cannot_tell_which_width_a_save_has_so_the_callers_width_decides)
{
    /* save_convert.h: SaveBlock1's last sector is zero past its 32-bit data,
     * so a 32-bit image passes the 64-bit chunk sizes' checksums (the way
     * back converts it, treating the 32-bit bytes as a 64-bit block); a
     * 64-bit one fails the 32-bit sizes' (nothing is converted, the test
     * above). The file header tells the caller. */
    static uint8_t expected0[SB1_CAPACITY];
    static uint8_t expected1[SB1_CAPACITY];
    make_image_32();
    expected_32_from_64(block0, expected0);
    expected_32_from_64(block1, expected1);

    PT_ASSERT_EQ_INT(save_convert_image(image, 64, 32), 2);
    check_slot(0, ROTATION_0, expected0, SB1_32);
    check_slot(1, ROTATION_1, expected1, SB1_32);
    check_special_sectors_untouched();
}

PTEST(bad_arguments_change_nothing)
{
    make_image_32();
    PT_ASSERT_EQ_INT(save_convert_image(NULL, 32, 64), SAVE_CONVERT_ERR_ARGUMENT);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 32), SAVE_CONVERT_ERR_ARGUMENT);
    PT_ASSERT_EQ_INT(save_convert_image(image, 64, 64), SAVE_CONVERT_ERR_ARGUMENT);
    PT_ASSERT_EQ_INT(save_convert_image(image, 16, 64), SAVE_CONVERT_ERR_ARGUMENT);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 0), SAVE_CONVERT_ERR_ARGUMENT);
    PT_ASSERT_EQ_INT(save_convert_image(image, 128, 32), SAVE_CONVERT_ERR_ARGUMENT);
    PT_ASSERT_MEM_EQ(image, original, CHIP_BYTES);
}

/* ---- 64 to 32 ---- */

/* make_image_32 with every object event template's script zero, which a
 * round trip restores (the scripts are not carried over). */
static void make_image_32_without_scripts(void)
{
    make_image_32();
    for (uint32_t slot = 0; slot < SLOTS; slot++) {
        uint8_t *block = slot == 0 ? block0 : block1;
        for (uint32_t t = 0; t < TEMPLATE_COUNT; t++)
            memset(block + TEMPLATES + t * TEMPLATE_32 + 16, 0, 4);
        write_slot(slot, slot == 0 ? ROTATION_0 : ROTATION_1, slot == 0 ? COUNTER_0 : COUNTER_1, block, SB1_32);
    }
    memcpy(original, image, sizeof image);
}

PTEST(the_way_back_restores_the_32_bit_save_but_its_scripts)
{
    /* The scripts do not survive (they are refilled from the map on
     * Continue); with them zero from the start the round trip is exact. */
    make_image_32_without_scripts();

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    PT_ASSERT_EQ_INT(save_convert_image(image, 64, 32), 2);
    PT_ASSERT_MEM_EQ(image, original, CHIP_BYTES);
}

PTEST(the_same_image_moved_to_64_and_back_twice_ends_where_it_began_and_agrees_with_itself)
{
    static uint8_t at_64[CHIP_BYTES];
    make_image_32_without_scripts();

    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    memcpy(at_64, image, sizeof image);
    PT_ASSERT_EQ_INT(save_convert_image(image, 64, 32), 2);
    PT_ASSERT_MEM_EQ(image, original, CHIP_BYTES);
    PT_ASSERT_EQ_INT(save_convert_image(image, 32, 64), 2);
    PT_ASSERT_MEM_EQ(image, at_64, CHIP_BYTES);
    PT_ASSERT_EQ_INT(save_convert_image(image, 64, 32), 2);
    PT_ASSERT_MEM_EQ(image, original, CHIP_BYTES);
}

PTEST(the_way_back_truncates_descriptions_and_recomputes_a_valid_enigma_checksum)
{
    static uint8_t block64[SB1_CAPACITY];
    static uint8_t block64_invalid[SB1_CAPACITY];
    make_image_32();
    expected_64(block0, block64);
    /* What a 64-bit build may hold: a hole, upper halves and tail padding
     * that are not zero (the sector checksum covers the padding: it is in
     * sizeof); the Enigma checksum over all 60 bytes. */
    memset(block64 + DATA_END_64, 0xEE, SB1_64 - DATA_END_64);
    uint8_t *record = block64 + ENIGMA_64;
    for (uint32_t i = 12; i < 16; i++)
        record[i] = (uint8_t)(0xA0 + i);
    for (uint32_t i = 20; i < 24; i++)
        record[i] = (uint8_t)(0xB0 + i);
    for (uint32_t i = 28; i < 32; i++)
        record[i] = (uint8_t)(0xC0 + i);
    put_u32(record + ENIGMA_SUMMED_64, enigma_sum(record, ENIGMA_SUMMED_64));
    memcpy(block64_invalid, block64, sizeof block64);
    put_u32(block64_invalid + ENIGMA_64 + ENIGMA_SUMMED_64, enigma_sum(record, ENIGMA_SUMMED_64) + 7);
    memset(image, 0xFF, sizeof image);
    write_slot(0, ROTATION_0, COUNTER_0, block64, SB1_64);
    write_slot(1, ROTATION_1, COUNTER_1, block64_invalid, SB1_64);
    memcpy(original, image, sizeof image);

    PT_ASSERT_EQ_INT(save_convert_image(image, 64, 32), 2);
    /* The 32-bit record: in chunk 3 (12720 - 3 * 3968 = 816). */
    const uint8_t *valid = sector_of_id(0, ROTATION_0, 4) + (ENIGMA_32 - 3 * SECTOR_DATA);
    const uint8_t *invalid = sector_of_id(1, ROTATION_1, 4) + (ENIGMA_32 - 3 * SECTOR_DATA);
    PT_ASSERT_MEM_EQ(valid, record, 12);
    PT_ASSERT_MEM_EQ(valid + 12, record + 16, 4); /* description1's low half */
    PT_ASSERT_MEM_EQ(valid + 16, record + 24, 4); /* description2's low half */
    PT_ASSERT_MEM_EQ(valid + 20, record + 32, 28);
    PT_ASSERT_EQ_U32(get_u32(valid + ENIGMA_SUMMED_32), enigma_sum(valid, ENIGMA_SUMMED_32));
    PT_ASSERT(get_u32(valid + ENIGMA_SUMMED_32) != get_u32(record + ENIGMA_SUMMED_64));
    PT_ASSERT_EQ_U32(get_u32(invalid + ENIGMA_SUMMED_32), enigma_sum(record, ENIGMA_SUMMED_64) + 7);
    /* The last chunk shrinks to 3592 bytes and the rest of the data area is zero. */
    const uint8_t *last = sector_of_id(0, ROTATION_0, 4);
    for (uint32_t i = 3592; i < SECTOR_DATA; i++)
        PT_ASSERT_EQ_U32(last[i], 0);
    PT_ASSERT_EQ_U32(get_u16(last + CHECKSUM_AT), calculate_checksum(last, 3592));
    check_special_sectors_untouched();
}

PTEST_MAIN()
