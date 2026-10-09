/*
 * layout_b_filled.ld: layout_b.ld with the gaps between the sections filled by
 * uninitialised, read-only sections (gaps.s), the second variant of probe W1
 * (research/m8-inventory.md section 2.2). Ascending order, adjacent sections.
 */
SECTIONS
{
    .gap0    0x01801000 : { *(.gap0) }
    .gbaewrm 0x02000000 : { *(.gbaewrm) }
    .gap1    0x02040000 : { *(.gap1) }
    .gbaiwrm 0x03000000 : { *(.gbaiwrm) }
    .gap2    0x03008000 : { *(.gap2) }
    .gbaio   0x04000000 : { *(.gbaio) }
    .gap3    0x04001000 : { *(.gap3) }
    .gbadbg  0x04FFF000 : { *(.gbadbg) }
    .gbapal  0x05000000 : { *(.gbapal) }
    .gap4    0x05001000 : { *(.gap4) }
    .gbavram 0x06000000 : { *(.gbavram) }
    .gap5    0x06018000 : { *(.gap5) }
    .gbaoam  0x07000000 : { *(.gbaoam) }
    .gap6    0x07001000 : { *(.gap6) }
    .gbarom  0x08000000 : { *(.gbarom) }
    .gap7    0x0A000000 : { *(.gap7) }
    .gbasram 0x0E000000 : { *(.gbasram) }
    .gap8    0x0E020000 : { *(.gap8) }
}
INSERT BEFORE .text;
