#!/bin/sh
# gbagfx_pad.sh: gbagfx, then, if the output is a .gbapal, zero-padded to a whole
# number of 32-byte palette banks (16 colours each). Usage as gbagfx: SRC DST [options].
#
# Many species palettes have 11 to 15 colours, yet LoadSpritePalette and LoadPaletteFast
# copy a whole bank, so the GBA read the next bytes of ROM (harmless); natively that is a
# read past the object (found by AddressSanitizer in the summary screen). The extra colours
# are never shown. Makefile.native uses this as GFX with its own assets directory, so the
# GBA build's palettes, and with them the ROM, are untouched.
real="$(dirname "$0")/../../tools/gbagfx/gbagfx"
"$real" "$@" || exit $?
case "$2" in
*.gbapal)
    size=$(stat -c %s "$2") || exit 1
    pad=$(( (32 - size % 32) % 32 ))
    if [ "$pad" -gt 0 ]; then
        head -c "$pad" /dev/zero >> "$2" || exit 1
    fi
    ;;
esac
