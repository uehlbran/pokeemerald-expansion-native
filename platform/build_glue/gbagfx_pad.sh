#!/bin/sh
# gbagfx_pad.sh: gbagfx, then, if the output is a .gbapal under $PAD_UNDER (the native
# assets directory), zero-padded to a whole number of 32-byte palette banks (16 colours
# each), unless its source file is in unpadded_palettes.txt. Usage as gbagfx: SRC DST [options].
#
# Many species palettes have 11 to 15 colours, yet LoadSpritePalette and LoadPaletteFast
# copy a whole bank, so the GBA read the next bytes of ROM (harmless); natively that is a
# read past the object (found by AddressSanitizer in the summary screen). The extra colours
# are never shown. Only files under PAD_UNDER are padded, so that a .gbapal that ever lands
# in the tree shared with the GBA build (and with it the ROM) is not touched.
# check_palette_padding.py guards the list and the sized uses.
here="$(dirname "$0")"
"$here/../../tools/gbagfx/gbagfx" "$@" || exit $?
case "$2" in
*.gbapal)
    case "$2" in
    "$PAD_UNDER"/*) ;;
    *) exit 0 ;;
    esac
    # a line is "path # reason"; match the source path exactly
    if sed -e 's/[[:space:]]*#.*//' -e '/^$/d' "$here/unpadded_palettes.txt" | grep -qxF -- "$1"; then
        exit 0
    fi
    size=$(stat -c %s "$2") || exit 1
    pad=$(( (32 - size % 32) % 32 ))
    if [ "$pad" -gt 0 ]; then
        head -c "$pad" /dev/zero >> "$2" || exit 1
    fi
    ;;
esac
