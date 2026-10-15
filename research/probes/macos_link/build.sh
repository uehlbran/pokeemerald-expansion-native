#!/usr/bin/env bash
# build.sh: probe M2 (research/probes/README.md, "Probe M2"), on macos-15-intel.
# Answers what the macOS port's design rests on and Linux cannot show: whether
# ld64 gives the platform's C names to section and segment bounds through
# -alias, keeps the sections of a segment in the order the first object
# declares them, aligns a marker section to 256 KiB, links absolute symbols,
# 4-byte absolute words, function aliases and undefined weak references
# (layout.s, game.c, main.c); the si_code of SIGFPE for each divide error;
# what the platform's flags make of <ucontext.h>; whether Apple clang's
# AddressSanitizer detects leaks; the relative condition wait under
# ThreadSanitizer; F_FULLFSYNC on the runner's disk; _NSGetExecutablePath.
# Every command is echoed with a "+"; everything goes to out/report.txt too.
# The script exits 0 after reporting: read the PASS/FAIL and RESULT lines.
set -u
cd "$(dirname "$0")"
rm -rf out
mkdir -p out
exec > >(tee out/report.txt) 2>&1

CFLAGS=(-std=gnu17 -O1 -g -Wall -Wextra)
STRICT=(-std=gnu17 -O1 -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion -Werror)

# The fixed-address image of probe M0 (research/probes/README.md: layout_nofixup).
IMAGE=(-Wl,-no_pie -Wl,-pagezero_size,0x1000 -Wl,-image_base,0x40000000 -Wl,-no_fixup_chains)
SEGMENTS=(
    -Wl,-segaddr,__GBA_CALLSTK,0x01800000
    -Wl,-segaddr,__GBA_EWRAM,0x02000000
    -Wl,-segaddr,__GBA_IWRAM,0x03000000
    -Wl,-segaddr,__GBA_IO,0x04000000
    -Wl,-segaddr,__GBA_DEBUG,0x04FFF000
    -Wl,-segaddr,__GBA_PALETTE,0x05000000
    -Wl,-segaddr,__GBA_VRAM,0x06000000
    -Wl,-segaddr,__GBA_OAM,0x07000000
    -Wl,-segaddr,__GBA_SRAM,0x0E000000
)
# Each pair: ld64's name for a bound (TARGET), then the C name the platform
# declares (underscored); -Wl,-alias,TARGET,NAME gives NAME TARGET's address.
BOUND_PAIRS=(
    'segment$start$__GBA_EWRAM' _gba_linked_ewram_start
    'section$start$__GBA_EWRAM$__ewram_end' _gba_linked_ewram_end
    'section$start$__GBA_EWRAM$__ewram_init' _gba_linked_ewram_init_start
    'section$end$__GBA_EWRAM$__ewram_init' _gba_linked_ewram_init_end
    'section$start$__GBA_EWRAM$__ewram_data' _gba_ewram_data_start
    'section$end$__GBA_EWRAM$__ewram_data' _gba_ewram_data_end
    'segment$start$__GBA_IWRAM' _gba_linked_iwram_start
    'segment$end$__GBA_IWRAM' _gba_linked_iwram_end
    'section$start$__GBA_IWRAM$__persistent' _gba_persistent_start
    'section$start$__GBA_IO$__io' _gba_linked_io_start
    'section$end$__GBA_IO$__io' _gba_linked_io_end
    'section$start$__GBA_SRAM$__sram' _gba_linked_sram_start
    'section$end$__GBA_SRAM$__sram' _gba_linked_sram_end
    'section$start$__GBA_CALLSTK$__stack' _platform_call_stack_start
    'section$end$__GBA_CALLSTK$__stack' _platform_call_stack_end
    'section$start$__DATA$__game_data' _game_data_start
    'section$end$__DATA$__game_data' _game_data_end
    'section$start$__DATA$__game_bss' _game_bss_start
    'section$end$__DATA$__game_bss' _game_bss_objects_end
    'section$end$__DATA$__game_common' _game_bss_end
    'section$start$__DATA$__tests' ___start_tests
    'section$end$__DATA$__tests' ___stop_tests
    'section$start$__DATA$__empty_probe' _probe_empty_start
    'section$end$__DATA$__empty_probe' _probe_empty_end
)
BOUNDS=()
for ((i = 0; i < ${#BOUND_PAIRS[@]}; i += 2)); do
    BOUNDS+=("-Wl,-alias,${BOUND_PAIRS[i]},${BOUND_PAIRS[i + 1]}")
done
# Not bounds: an absolute symbol, the image's header, a function; and the weak
# references nothing defines, which -U lets stay undefined.
OTHERS=(
    -Wl,-alias,_probe_one,_gba_game_objects_linked
    -Wl,-alias,__mh_execute_header,_PlatformAllocationSiteBase
    -Wl,-alias,_CB2_Probe,_gInitialMainCB2
    -Wl,-U,_gPlatformTestEntrySize
    -Wl,-U,_PlatformTestTableBadEntry
)

run() { echo "+ $*"; "$@"; }

echo "== environment"
run sw_vers
run uname -m
run clang --version
run ld -v
run xcrun --show-sdk-version
run nm --version
run sysctl -n hw.pagesize

# variant NAME [-DPROBE_ASM_LABELS] [--asan] [--layout-last] [extra link flags...]
variant() {
    local name=$1; shift
    local defines=() sanitize=() order=first link=()
    while [ $# -gt 0 ]; do
        case $1 in
            -D*) defines+=("$1") ;;
            --asan) sanitize=(-fsanitize=address) ;;
            --layout-last) order=last ;;
            *) link+=("$1") ;;
        esac
        shift
    done
    echo "---- variant $name"
    mkdir -p "out/$name"
    run clang -arch x86_64 -c layout.s -o "out/$name/layout.o" || { echo "RESULT $name: BUILD FAILED"; return; }
    run clang -arch x86_64 "${CFLAGS[@]}" ${sanitize[@]+"${sanitize[@]}"} -c game.c -o "out/$name/game.o" \
        || { echo "RESULT $name: BUILD FAILED"; return; }
    run clang -arch x86_64 "${CFLAGS[@]}" ${sanitize[@]+"${sanitize[@]}"} ${defines[@]+"${defines[@]}"} \
        -c main.c -o "out/$name/main.o" || { echo "RESULT $name: BUILD FAILED"; return; }
    local objects=("out/$name/layout.o" "out/$name/game.o" "out/$name/main.o")
    [ "$order" = last ] && objects=("out/$name/game.o" "out/$name/main.o" "out/$name/layout.o")
    if ! run clang -arch x86_64 ${sanitize[@]+"${sanitize[@]}"} "${objects[@]}" -o "out/$name/probe" \
        "${IMAGE[@]}" "${SEGMENTS[@]}" ${link[@]+"${link[@]}"} "${OTHERS[@]}"; then
        echo "RESULT $name: LINK FAILED"
        return
    fi
    run "out/$name/probe"
    echo "RESULT $name: exit status $?"
}

echo "== the link"
variant alias "${BOUNDS[@]}"
variant asmlabel -DPROBE_ASM_LABELS
variant alias_asan --asan "${BOUNDS[@]}"
variant alias_layout_last --layout-last "${BOUNDS[@]}"
variant alias_classic "${BOUNDS[@]}" -Wl,-ld_classic
if [ -x out/alias/probe ]; then
    echo "+ nm -p --defined-only --format=sysv out/alias/probe (head)"
    nm -p --defined-only --format=sysv out/alias/probe | head -40
fi

echo "== SIGFPE"
run clang -arch x86_64 "${CFLAGS[@]}" signals.c -o out/signals && run out/signals
echo "RESULT signals: exit status $?"
run clang -arch x86_64 "${CFLAGS[@]}" -fsanitize=address,undefined signals.c -o out/signals_asan && run out/signals_asan
echo "RESULT signals_asan: exit status $?"

echo "== ucontext under the platform's flags"
run clang -arch x86_64 "${STRICT[@]}" -c ucontext_plain.c -o out/ucontext_plain.o
echo "RESULT ucontext_plain (no _XOPEN_SOURCE, -Werror): exit status $?"
run clang -arch x86_64 "${STRICT[@]}" -c ucontext_xopen.c -o out/ucontext_xopen.o
echo "RESULT ucontext_xopen (-Werror): exit status $?"
run clang -arch x86_64 "${CFLAGS[@]}" -fsanitize=address ucontext_xopen.c -o out/ucontext_xopen && run out/ucontext_xopen
echo "RESULT ucontext_xopen run (warnings allowed, ASan): exit status $?"

echo "== LeakSanitizer"
run clang -arch x86_64 "${CFLAGS[@]}" -fsanitize=address lsan.c -o out/lsan
echo "+ ASAN_OPTIONS=detect_leaks=1 out/lsan"
ASAN_OPTIONS=detect_leaks=1 out/lsan
echo "RESULT lsan detect_leaks=1: exit status $?"
run out/lsan
echo "RESULT lsan default: exit status $?"

echo "== condition wait"
run clang -arch x86_64 "${CFLAGS[@]}" condwait.c -o out/condwait && run out/condwait
echo "RESULT condwait: exit status $?"
run clang -arch x86_64 "${CFLAGS[@]}" -fsanitize=thread condwait.c -o out/condwait_tsan
echo "+ TSAN_OPTIONS=halt_on_error=1:exitcode=66 out/condwait_tsan"
TSAN_OPTIONS=halt_on_error=1:exitcode=66 out/condwait_tsan
echo "RESULT condwait_tsan: exit status $?"

echo "== F_FULLFSYNC"
run clang -arch x86_64 "${CFLAGS[@]}" fullfsync.c -o out/fullfsync && mkdir -p out/disk && run out/fullfsync "$PWD/out/disk"
echo "RESULT fullfsync: exit status $?"
run df -h "$PWD/out/disk"
run diskutil info / | grep -E "File System Personality|Solid State|Protocol|Device Location" || true

echo "== _NSGetExecutablePath"
run clang -arch x86_64 "${CFLAGS[@]}" exepath.c -o out/exepath
run ./out/exepath
run out/exepath
ln -s exepath out/exepath_link
run out/exepath_link
(cd / && run "$OLDPWD/out/exepath")

echo "== Homebrew"
export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1
run brew --version
run brew list --versions
run brew info --formula x86_64-elf-binutils sdl2 sdl2-compat make coreutils libpng pkg-config
exit 0
