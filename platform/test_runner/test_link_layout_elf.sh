#!/bin/bash
# Tests gba_regions.ld's rule for where the writable data of an ELF executable
# may lie: not where an address's bits 24-27 read as EWRAM's (0x42000000-
# 0x42FFFFFF, then 0x52000000 and up), which expansion would take for EWRAM
# (src/librfu_rfu.c; gba_regions.ld says why). Small programs are linked as
# the game is (-no-pie, the image at 0x40000000, both scripts) with their
# read-only data moved (-Trodata-segment) so that the writable data after it
# falls below, into, across or past a window: below links, into or across
# fails the link with the script's message, and gba_writable_data_floor
# (what target.mk sets for the sanitized builds) moves it to 0x43000000,
# which links. Both widths. Runs on Linux (`make -C platform tools-test`).
set -euo pipefail
platform=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/t.c" <<'C'
const char gConstant[16] = "read-only";
char gData[16] = "writable";
char gBss[BSS_SIZE];
int main(void) { return gConstant[0] + gData[0] + gBss[0]; }
C
failures=0
# expect BITS links|refused READ_ONLY_AT BSS_SIZE [FLAGS...]: the link's outcome, and
# for a link, that its writable data is out of every window.
expect() {
    local bits=$1 wanted=$2 rodata=$3 bss=$4
    shift 4
    local outcome=linked
    "$cc" -m"$bits" -O1 -DBSS_SIZE="$bss" -no-pie -Wl,-Ttext-segment=0x40000000 -Wl,-Trodata-segment="$rodata" \
        -Wl,-T,"$platform/gba_regions.ld" -Wl,-T,"$platform/gba_game.ld" "$@" -o "$tmp/t" "$tmp/t.c" 2> "$tmp/err" \
        || outcome=refused
    if [ "$outcome" = refused ] && ! grep -q "whose bits 24-27 read as EWRAM's" "$tmp/err"; then
        echo "FAILED: -m$bits, read-only data at $rodata: the link failed otherwise:" >&2
        cat "$tmp/err" >&2
        failures=$((failures + 1))
        return
    fi
    if [ "$outcome" != "$wanted" ]; then
        echo "FAILED: -m$bits, read-only data at $rodata, $bss bytes of .bss $*: $outcome, expected $wanted" >&2
        failures=$((failures + 1))
        return
    fi
    if [ "$outcome" = linked ]; then
        # Every writable PT_LOAD of the image (0x40000000 and up) clear of the windows.
        if ! readelf -lW "$tmp/t" | awk '$1 == "LOAD" && $7 ~ /W/ {
                start = strtonum($3); end = start + strtonum($6)
                if (start < 0x40000000) next
                for (w = 0x42000000; w < 0x100000000; w += 0x10000000)
                    if (start < w + 0x1000000 && end > w) { printf "0x%x-0x%x meets 0x%x\n", start, end, w; bad = 1 }
            } END { exit bad }'; then
            echo "FAILED: -m$bits, read-only data at $rodata: linked with writable data in a window" >&2
            failures=$((failures + 1))
            return
        fi
    fi
    echo "-m$bits, read-only data at $rodata, .bss $bss $*: $outcome"
}
floor=-Wl,--defsym,gba_writable_data_floor=0x43000000
for bits in 64 32; do
    expect "$bits" linked 0x41000000 16
    expect "$bits" linked 0x41F00000 0xF0000
    expect "$bits" refused 0x41F00000 0x200000
    expect "$bits" refused 0x42500000 16
    expect "$bits" linked 0x42500000 16 "$floor"
    expect "$bits" linked 0x41000000 16 "$floor"
    expect "$bits" linked 0x43000000 0x0E000000
    expect "$bits" refused 0x43000000 0x0F100000
done
if [ "$failures" -ne 0 ]; then
    echo "test_link_layout_elf.sh: $failures failed" >&2
    exit 1
fi
echo "test_link_layout_elf.sh: the writable data stays out of the EWRAM windows"
