#!/usr/bin/env bash
# all_rebase.sh: rebasefilter.py over the pre-optimisation IR of every src/*.c
# (divfilter/expansion_ir.sh's out/expansion/*.{x86_64,aarch64}.ll), then
# each filtered file compiled at -O2; totals of the rewrites and failures.
set -u
cd "$(dirname "$0")"
mkdir -p out/all
one() {
    ll=$1; b=$(basename "$ll" .ll)
    case $b in *.x86_64) t=x86_64-linux-gnu ;; *) t=aarch64-linux-gnu ;; esac
    if ! python3 -I rebasefilter.py "$ll" "out/all/$b.rebased.ll" 2> "out/all/$b.log"; then
        echo "REFUSED $b" >> out/all/status.txt; return; fi
    if clang --target=$t -O2 -w -c "out/all/$b.rebased.ll" -o "out/all/$b.o" 2>> "out/all/$b.log"; then
        echo "OK $b" >> out/all/status.txt
    else
        echo "COMPILE-FAILED $b" >> out/all/status.txt
    fi
    rm -f "out/all/$b.rebased.ll"
}
export -f one
rm -f out/all/status.txt
ls ../divfilter/out/expansion/*.x86_64.ll ../divfilter/out/expansion/*.aarch64.ll | xargs -P 16 -I{} bash -c 'one {}'
cut -d' ' -f1 out/all/status.txt | sort | uniq -c
grep -v "^OK" out/all/status.txt
for t in x86_64 aarch64; do
    echo "== $t rewrites"
    cat out/all/*.$t.log | grep "^rebasefilter" | sed 's/^rebasefilter [^:]*: //' | awk -F': ' '{s[$1]+=$2} END{for(k in s) print s[k], k}' | sort -k2
done
