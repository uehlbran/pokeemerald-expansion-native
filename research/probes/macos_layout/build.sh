#!/usr/bin/env bash
# build.sh: probe M0 (research/m8-inventory.md 2.4, 2.5, 5.3). Builds the layout probe
# with clang and ld64 as a fixed-address x86_64 image with the GBA regions as
# segments, and runs it 100 times; also the MAP_FIXED probe. Every command is echoed
# with a "+" prefix; everything is also written to out/report.txt.
#   On macos-15-intel the layout probe is expected to link and run. Four link
#   attempts: as it was (layout_plain), -no_fixup_chains, an old deployment target and
#   -ld_classic; ld ignores -image_base with chained fixups, so the header address
#   in each run's output says which one worked.
#   On macos-latest (arm64) it is expected to be refused or to fail to run; this
#   records what happens, then tries the x86_64 build under Rosetta 2.
set -u
cd "$(dirname "$0")"
mkdir -p out
exec > >(tee out/report.txt) 2>&1

RUNS=100
IMAGE_BASE=0x40000000
PAGEZERO_SIZE=0x1000
CFLAGS="-std=c11 -O1 -Wall -Wextra -Wshadow -Wconversion -Wstrict-prototypes -Werror"
HOST_ARCH=$(uname -m)

# Region segments at their GBA addresses; GBATEK "GBA Memory Map", regions.S.
SEGADDRS=(
    -Wl,-segaddr,__GBAEWRAM,0x02000000
    -Wl,-segaddr,__GBAIWRAM,0x03000000
    -Wl,-segaddr,__GBAIO,0x04000000
    -Wl,-segaddr,__GBADEBUG,0x04FFF000
    -Wl,-segaddr,__GBAPAL,0x05000000
    -Wl,-segaddr,__GBAVRAM,0x06000000
    -Wl,-segaddr,__GBAOAM,0x07000000
    -Wl,-segaddr,__GBASRAM,0x0E000000
)

run() { echo "+ $*"; "$@"; }

echo "== environment (host arch: $HOST_ARCH)"
run uname -a
run sw_vers
run clang --version
run xcodebuild -version
run ld -v
run sysctl -n machdep.cpu.brand_string
run sysctl -n hw.pagesize
if [ "$HOST_ARCH" = arm64 ]; then
    run arch -x86_64 /usr/bin/true
    echo "Rosetta present: exit status $?"
fi

# link_layout NAME ARCH MINFLAG LINKFLAGS...: the layout probe for ARCH. MINFLAG is a
# deployment-target flag for the compiler and the linker, or "" for none.
link_layout() {
    local name=$1 arch=$2 minflag=$3; shift 3
    local min=()
    [ -n "$minflag" ] && min=("$minflag")
    run clang -arch "$arch" ${min[@]+"${min[@]}"} $CFLAGS -c probe.c -o "out/$name.probe.o" || return 1
    run clang -arch "$arch" ${min[@]+"${min[@]}"} -c regions.S -o "out/$name.regions.o" || return 1
    run clang -arch "$arch" ${min[@]+"${min[@]}"} "out/$name.probe.o" "out/$name.regions.o" -o "out/$name" \
        -Wl,-no_pie -Wl,-pagezero_size,$PAGEZERO_SIZE -Wl,-image_base,$IMAGE_BASE \
        "${SEGADDRS[@]}" "$@" || return 1
    # Chained fixups make ld ignore -image_base (run 37922990222, probe_macos.log:253).
    if otool -l "out/$name" | grep -q LC_DYLD_CHAINED_FIXUPS; then
        echo "LC_DYLD_CHAINED_FIXUPS: present in $name"
    else
        echo "LC_DYLD_CHAINED_FIXUPS: absent in $name"
    fi
    echo "+ otool -l out/$name (build version, dyld info, __TEXT)"
    otool -l "out/$name" | grep -E "LC_BUILD_VERSION|minos|LC_DYLD|segname __TEXT" -A1
}

# count_runs NAME PREFIX...: run PREFIX out/NAME 100 times, count exits with status 0.
count_runs() {
    local name=$1; shift
    local ok=0 bad=0 codes="" i status
    echo "---- $name: first run"
    "$@" "./out/$name"
    echo "exit status of the first run: $?"
    for ((i = 1; i <= RUNS; i++)); do
        "$@" "./out/$name" > "out/$name.run.txt" 2>&1
        status=$?
        if [ "$status" -eq 0 ]; then
            ok=$((ok + 1))
        else
            bad=$((bad + 1)); codes="$codes $status"
            cp "out/$name.run.txt" "out/$name.failed_run_$i.txt"
        fi
    done
    echo "RESULT $name: $ok of $RUNS runs exited 0, $bad did not (statuses:$codes)"
}

echo "== layout probe, $HOST_ARCH, -no_pie -pagezero_size $PAGEZERO_SIZE -image_base $IMAGE_BASE + segments"
# x86_64 macOS accepts a 10.13 target; arm64 starts at 11.0.
if [ "$HOST_ARCH" = arm64 ]; then OLD_MIN=-mmacosx-version-min=11.0; else OLD_MIN=-mmacosx-version-min=10.13; fi
try_layout() { # NAME MINFLAG LINKFLAGS...
    local name=$1
    echo "---- attempt $name"
    if link_layout "$name" "$HOST_ARCH" "${@:2}"; then
        echo "LINKED $name"
        count_runs "$name"
    else
        echo "LINK FAILED $name"
    fi
}
try_layout layout_plain ""
try_layout layout_nofixup "" -Wl,-no_fixup_chains
try_layout layout_minos "$OLD_MIN"
try_layout layout_classic "" -Wl,-ld_classic

echo "== MAP_FIXED probe at 0x02000000 and 0x04000000, default page zero and a 4 KiB one"
run clang -arch "$HOST_ARCH" $CFLAGS -c mapfixed.c -o out/mapfixed.o
run clang -arch "$HOST_ARCH" out/mapfixed.o -o out/mapfixed_default
count_runs mapfixed_default
if run clang -arch "$HOST_ARCH" out/mapfixed.o -o out/mapfixed_smallpz -Wl,-pagezero_size,$PAGEZERO_SIZE; then
    count_runs mapfixed_smallpz
else
    echo "LINK FAILED mapfixed_smallpz"
fi

if [ "$HOST_ARCH" = arm64 ]; then
    echo "== the same layout probe built for x86_64 and run under Rosetta 2"
    if link_layout layout_rosetta x86_64 "" -Wl,-no_fixup_chains; then
        echo "LINKED layout_rosetta"
        count_runs layout_rosetta arch -x86_64
    else
        echo "LINK FAILED layout_rosetta"
    fi
    run clang -arch x86_64 mapfixed.c -o out/mapfixed_rosetta
    count_runs mapfixed_rosetta arch -x86_64
fi
exit 0
