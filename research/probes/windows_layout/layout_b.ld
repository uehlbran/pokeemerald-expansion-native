/*
 * layout_b.ld: "layout B" of research/m8-inventory.md section 2.2. The image base is
 * 0x01800000 (--image-base), each GBA region is a PE section at its GBA address, and
 * the code is at 0x40001000 (--section-start=.text=0x40001000), below 0x42000000 as
 * platform/script_tag.h needs. INSERT BEFORE .text keeps the section table in
 * ascending address order, which the PE format asks for.
 * Addresses: GBATEK "GBA Memory Map"; platform/gba_regions.ld has the same list.
 */
SECTIONS
{
    .gbaewrm 0x02000000 : { *(.gbaewrm) }
    .gbaiwrm 0x03000000 : { *(.gbaiwrm) }
    .gbaio   0x04000000 : { *(.gbaio) }
    .gbadbg  0x04FFF000 : { *(.gbadbg) }
    .gbapal  0x05000000 : { *(.gbapal) }
    .gbavram 0x06000000 : { *(.gbavram) }
    .gbaoam  0x07000000 : { *(.gbaoam) }
    .gbarom  0x08000000 : { *(.gbarom) }
    .gbasram 0x0E000000 : { *(.gbasram) }
}
INSERT BEFORE .text;
