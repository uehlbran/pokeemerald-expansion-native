#!/usr/bin/env bash
# build.sh: probe W1 (research/m8-inventory.md 2.2, 5.2). Builds the three variants of
# probe.c with the MinGW-w64 gcc and ld on PATH and runs each 100 times. Run it on a
# Windows runner (the workflow does); every command is echoed with a "+" prefix, and
# everything is also written to out/report.txt.
#   as_linked        layout B as linked (gaps between the sections)
#   filled_gaps      layout B with the gaps filled by uninitialised read-only sections
#   virtualalloc     no region sections; main calls VirtualAlloc at the GBA addresses
set -u
cd "$(dirname "$0")"
mkdir -p out
exec > >(tee out/report.txt) 2>&1

RUNS=100
IMAGE_BASE=0x01800000
CODE_ADDRESS=0x40001000
CFLAGS="-std=c11 -O1 -Wall -Wextra -Wshadow -Wconversion -Wstrict-prototypes -Werror -D__USE_MINGW_ANSI_STDIO=1"
# No relocations and no randomisation: the loader maps the exe at its base or refuses.
LDFLAGS="-Wl,--image-base=$IMAGE_BASE -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va -Wl,--disable-reloc-section -Wl,--section-start=.text=$CODE_ADDRESS"
GAP_SECTIONS=".gap0 .gap1 .gap2 .gap3 .gap4 .gap5 .gap6 .gap7 .gap8"

run() { echo "+ $*"; "$@"; }

echo "== environment"
run uname -a
run gcc --version
run ld --version
run objcopy --version
run objdump --version
cmd.exe //c ver || true
echo "== sections of the regions: objcopy makes the ROM and the gap sections read-only"
run gcc -c regions.s -o out/regions.o
run objcopy --set-section-flags .gbarom=alloc,readonly out/regions.o
run gcc -c gaps.s -o out/gaps.o
GAP_FLAGS=()
for s in $GAP_SECTIONS; do GAP_FLAGS+=(--set-section-flags "$s=alloc,readonly"); done
run objcopy "${GAP_FLAGS[@]}" out/gaps.o

echo "== build"
BUILT=()
build_variant() { # name, then the gcc command's remaining arguments
    local name=$1; shift
    if run gcc $CFLAGS "$@" -o "out/$name.exe" -lpsapi; then
        BUILT+=("$name")
        run objdump -h "out/$name.exe"
    else
        echo "BUILD FAILED: $name"
    fi
}
build_variant as_linked -DLAYOUT_IMAGE probe.c out/regions.o $LDFLAGS -Wl,-T,layout_b.ld
build_variant filled_gaps -DLAYOUT_IMAGE probe.c out/regions.o out/gaps.o $LDFLAGS -Wl,-T,layout_b_filled.ld
build_variant virtualalloc -DLAYOUT_RUNTIME probe.c
# The reference for the commit-charge figures: same CRT, no regions.
run gcc $CFLAGS baseline.c -o out/baseline.exe -lpsapi

echo "== run: each variant once in full, then $RUNS starts counted"
for name in "${BUILT[@]}"; do
    echo "---- $name: first run"
    "./out/$name.exe"
    echo "exit status of the first run: $?"
    if [ "$name" = as_linked ]; then
        # Git Bash reports a refused image as "Exec format error"; ask Windows itself.
        echo "---- $name: start status as Windows reports it"
        # Each start is bounded: run 2 of the probes hung inside cmd.exe here until the
        # job's timeout (an orphan cmd process), so nothing waits on a console or a dialog.
        timeout 30 cmd.exe //c "out\\$name.exe" < /dev/null
        echo "cmd.exe exit status: $?"
        timeout 60 powershell.exe -NoProfile -NonInteractive -Command "try { & '.\\out\\$name.exe'; Write-Output ('exit ' + \$LASTEXITCODE) } catch { Write-Output ('exception: ' + \$_.Exception.Message + ' HResult=' + \$_.Exception.HResult) }" < /dev/null
        echo "powershell exit status: $?"
    fi
    : > "out/$name.bases.txt"
    ok=0
    bad=0
    codes=""
    for ((i = 1; i <= RUNS; i++)); do
        "./out/$name.exe" > "out/$name.run.txt" 2>&1
        status=$?
        grep '^image base:' "out/$name.run.txt" | sed 's/ (expected.*//' >> "out/$name.bases.txt"
        if [ "$status" -eq 0 ]; then
            ok=$((ok + 1))
        else
            bad=$((bad + 1))
            codes="$codes $status"
            cp "out/$name.run.txt" "out/$name.failed_run_$i.txt"
        fi
    done
    echo "RESULT $name: $ok of $RUNS runs exited 0, $bad did not (statuses:$codes)"
    echo "image base over the $RUNS runs (count, value):"
    sort "out/$name.bases.txt" | uniq -c
done

echo "== commit charge of the reference process (same CRT, no regions)"
run ./out/baseline.exe

echo "== results"
for name in as_linked filled_gaps virtualalloc; do
    case " ${BUILT[*]} " in *" $name "*) ;; *) echo "RESULT $name: did not build/link";; esac
done
exit 0
