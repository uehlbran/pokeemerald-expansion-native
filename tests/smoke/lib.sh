# tests/smoke/lib.sh: the engine shared by the smoke gates (run.sh: gate (d), the
# opening; run_debug_menu.sh: gate (e), the debug menu). Sourced, not run.
#
# A gate script sets, then calls `smoke_main "$@"`:
#   SMOKE_NAME        a title for messages
#   SMOKE_SCRIPT      the replay script
#   SMOKE_EXPECTED    the directory of checked-in expectations
#   SMOKE_USAGE       the usage line
#   SMOKE_ROUND_TRIP  words "BEFORE_DUMP:AFTER_DUMP:fact,fact,..." each: facts that must be equal
#                     in the two dumps (what was saved against what Continue loaded)
#
#   run.sh BINARY OUTDIR            run and compare; exit 0 if identical
#   run.sh BINARY OUTDIR --update   run, then rewrite the expectations
#
# Builds nothing. BINARY is build/native32/pokeemerald-native (or the
# SANITIZE=1 one, build/native32-san/pokeemerald-native-san; run it with
# ASAN_OPTIONS=halt_on_error=0 so that every finding is listed, not only the
# first). OUTDIR gets a fresh work/ directory with the replay's screenshots
# (.ppm), dumps (.txt), the save file and the game's stdout and stderr.
#
# What is compared (README.md has the reasons):
#   - every dump against <expected>/<name>.txt, fact by fact; the
#     `frame` fact is left out, it is a count of this build's timing;
#   - every screenshot's md5 against <expected>/screenshots.md5;
#   - the save round trips of SMOKE_ROUND_TRIP.
# Sanitizer reports in the game's stderr are listed and fail the run, except
# the lines matched by tests/smoke/sanitizer-known.txt (if present).
# Not `set -e`: the game's exit status, grep and diff results are tested on
# purpose and every failure is counted and reported; `-u` catches typos.
export TZ=UTC # belt and braces: --rtc-fixed already makes the clock independent of the zone

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Seconds since the epoch the replay's clock starts at (--rtc-fixed): 2023-11-14 22:13:20 UTC.
rtc_epoch=1700000000
volatile_facts='^frame='

die() { echo "$SMOKE_NAME: $*" >&2; exit 2; }

smoke_main() {
script="$SMOKE_SCRIPT"
expected="$SMOKE_EXPECTED"
[ $# -ge 2 ] || die "usage: $SMOKE_USAGE"
binary="$1"
outdir="$2"
# an empty OUTDIR would make work="/work" and `rm -rf` it
case "$outdir" in "" | "/") die "OUTDIR must be a directory of its own, not '$outdir'" ;; esac
update=0
case "${3:-}" in
"") ;;
--update) update=1 ;;
*) die "unknown argument '$3' (usage: $SMOKE_USAGE)" ;;
esac
[ $# -le 3 ] || die "too many arguments (usage: $SMOKE_USAGE)"
[ -x "$binary" ] || die "no executable $binary (build it first; see docs/BUILDING.md)"

work="$outdir/work"
rm -rf "$work"
mkdir -p "$work" || die "cannot create $work"

echo "== replaying $script with $binary"
"$binary" --headless --silent --unthrottled \
    --replay "$script" --out "$work" --rtc-fixed "$rtc_epoch" \
    --save "$work/pokeemerald.sav" --rtc-offset "$work/pokeemerald.rtc" \
    > "$work/stdout.log" 2> "$work/stderr.log"
status=$?
echo "replay exit status $status; $(grep -c '^main: ' "$work/stdout.log") process(es)"
failures=0

if [ "$status" -ne 0 ]; then
    echo "FAIL: the replay did not end with status 0:"
    grep -E '^(replay|main|restart|symbols):' "$work/stderr.log" | head -20
    failures=$((failures + 1))
fi

# Sanitizer findings, minus the known ones.
sanitizer_lines() {
    grep -E 'runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer|SUMMARY: (Address|Undefined|Leak)Sanitizer' "$work/stderr.log"
}
known="$here/sanitizer-known.txt"
if [ -f "$known" ]; then
    new_findings="$(sanitizer_lines | grep -v -F -f <(grep -v '^#' "$known" | grep -v '^$') || true)"
else
    new_findings="$(sanitizer_lines || true)"
fi
if [ -n "$new_findings" ]; then
    echo "FAIL: sanitizer reports in $work/stderr.log:"
    echo "$new_findings" | sort | uniq -c | sort -rn | head -40
    failures=$((failures + 1))
fi

facts_of() { grep -v -E "$volatile_facts" "$1"; }

if [ "$update" -eq 1 ]; then
    [ "$failures" -eq 0 ] || die "not updating the expectations from a failing run"
    rm -rf "$expected"
    mkdir -p "$expected"
    for dump in "$work"/*.txt; do
        facts_of "$dump" > "$expected/$(basename "$dump")"
    done
    (cd "$work" && md5sum -- *.ppm) > "$expected/screenshots.md5"
    echo "updated $expected: $(ls "$expected"/*.txt | wc -l) dumps, $(wc -l < "$expected/screenshots.md5") screenshots"
    exit 0
fi

[ -d "$expected" ] || die "no $expected; run with --update once"

# Dumps.
for want in "$expected"/*.txt; do
    name="$(basename "$want")"
    got="$work/$name"
    if [ ! -f "$got" ]; then
        echo "FAIL: the replay did not write $name"
        failures=$((failures + 1))
        continue
    fi
    if ! diff <(facts_of "$got") "$want" > "$work/$name.diff"; then
        echo "FAIL: $name differs (got <, expected >):"
        cat "$work/$name.diff"
        failures=$((failures + 1))
    else
        rm -f "$work/$name.diff"
    fi
done
for got in "$work"/*.txt; do
    [ -f "$expected/$(basename "$got")" ] || { echo "FAIL: $(basename "$got") is new (no expectation); run --update"; failures=$((failures + 1)); }
done

# Screenshots, by md5.
if ! diff <(cd "$work" && md5sum -- *.ppm) "$expected/screenshots.md5" > "$work/screenshots.diff"; then
    echo "FAIL: screenshots differ (got <, expected >; the PPMs are in $work):"
    cat "$work/screenshots.diff"
    failures=$((failures + 1))
else
    rm -f "$work/screenshots.diff"
fi

# The save round trips: what the game saved is what Continue loaded.
fact() { grep "^$2=" "$1" | cut -d= -f2-; }
for trip in $SMOKE_ROUND_TRIP; do
    IFS=: read -r before_dump after_dump keys <<< "$trip"
    for key in ${keys//,/ }; do
        before="$(fact "$work/$before_dump.txt" "$key")"
        after="$(fact "$work/$after_dump.txt" "$key")"
        if [ -z "$before" ] || [ "$before" != "$after" ]; then
            echo "FAIL: $key was '$before' when saved and '$after' after the restart"
            failures=$((failures + 1))
        fi
    done
done

if [ "$failures" -eq 0 ]; then
    echo "PASS: $(ls "$expected"/*.txt | wc -l) dumps, $(wc -l < "$expected/screenshots.md5") screenshots, the save round trip"
    exit 0
fi
echo "FAILED: $failures problem(s); outputs are in $work"
exit 1
}
