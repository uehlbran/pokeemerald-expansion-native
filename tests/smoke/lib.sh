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
set -u
export TZ=UTC # belt and braces: --rtc-fixed already makes the clock independent of the zone

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Seconds since the epoch the replay's clock starts at (--rtc-fixed): 2023-11-14 22:13:20 UTC.
rtc_epoch=1700000000
volatile_facts='^frame='

die() { echo "$SMOKE_NAME: $*" >&2; exit 2; }

# A failed check: report it and count it. The gate's exit status is decided by `failures`.
failures=0
fail() {
    echo "FAIL: $*"
    failures=$((failures + 1))
}

# grep's status 1 is "no match", which is an answer; anything above is an error that must not
# pass for one. Runs grep with the arguments, prints the matches.
grep_or_die() {
    grep "$@"
    local status=$?
    [ "$status" -le 1 ] || die "grep failed with status $status"
    return 0
}

# What differs between Linux and Git Bash on Windows, where the gates also run
# (docs/BUILDING.md, "Running it on Windows"), in three functions:
# md5 checksums in the form Linux's md5sum writes ("<md5>  <name>"; Git
# Bash's md5sum marks every file as read in binary mode, "<md5> *<name>"),
# one line per file named in the current directory;
md5_list() {
    local file sum
    for file; do
        sum="$(md5sum < "$file")" || return 1
        printf '%s  %s\n' "${sum%% *}" "$file"
    done
}
# the md5 of file $1;
md5_of() {
    local sum
    sum="$(md5sum < "$1")" || return 1
    echo "${sum%% *}"
}
# the size of file $1 in bytes (stat -c is GNU's, wc -c everyone's).
file_size() {
    local size
    size="$(wc -c < "$1")" || return 1
    echo $((size))
}

# The facts of dump $1 that are compared: all but the volatile ones.
facts_of() { grep_or_die -v -E "$volatile_facts" "$1"; }

# The value of fact $2 in dump $1.
fact() { grep_or_die "^$2=" "$1" | cut -d= -f2-; }

# The sanitizer reports in the game's stderr $1, minus the known ones, written to $2 (and the
# full list to $2.all); true if any is left. Files, not substitutions, so that a failing grep
# stops the run.
new_sanitizer_reports() {
    local stderr_log="$1"
    local new_findings="$2"
    local findings="$2.all"
    grep_or_die -E 'runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer|SUMMARY: (Address|Undefined|Leak)Sanitizer' \
        "$stderr_log" > "$findings"
    local known="$here/sanitizer-known.txt"
    if [ -f "$known" ]; then
        local patterns="$new_findings.known-patterns"
        grep_or_die -v -e '^#' -e '^$' "$known" > "$patterns"
        grep_or_die -v -F -f "$patterns" "$findings" > "$new_findings"
    else
        cp "$findings" "$new_findings" || die "cannot copy $findings"
    fi
    [ -s "$new_findings" ]
}

# Runs binary $1 headless in directory $2 with replay $3 and the save and RTC files there, plus
# arguments $4...; checks the exit status and the sanitizer reports.
play() {
    local binary="$1" dir="$2" script="$3"
    shift 3
    echo "== $(basename "$dir"): $binary, $(basename "$script") $*"
    "$binary" --headless --silent --unthrottled --replay "$script" --out "$dir" \
        --save "$dir/pokeemerald.sav" --rtc-offset "$dir/pokeemerald.rtc" "$@" \
        > "$dir/stdout.log" 2> "$dir/stderr.log"
    local status=$?
    echo "replay exit status $status; $(grep_or_die -c '^main: ' "$dir/stdout.log") process(es)"
    if [ "$status" -ne 0 ]; then
        fail "$(basename "$dir"): the replay ended with status $status:"
        grep -E '^(replay|main|restart|symbols|save_file):' "$dir/stderr.log" | head -20
    fi
    if new_sanitizer_reports "$dir/stderr.log" "$dir/sanitizer-new.log"; then
        fail "$(basename "$dir"): sanitizer reports in $dir/stderr.log:"
        sort "$dir/sanitizer-new.log" | uniq -c | sort -rn | head -40
    fi
}

# Facts $3... equal in dumps $1 and $2.
same_facts() {
    local before_dump="$1" after_dump="$2" key before after
    shift 2
    for key in "$@"; do
        before="$(fact "$before_dump" "$key")"
        after="$(fact "$after_dump" "$key")"
        if [ -z "$before" ] || [ "$before" != "$after" ]; then
            fail "$key was '$before' in $before_dump and '$after' in $after_dump"
        fi
    done
}

smoke_main() {
local script="$SMOKE_SCRIPT"
local expected="$SMOKE_EXPECTED"
[ $# -ge 2 ] || die "usage: $SMOKE_USAGE"
local binary="$1"
local outdir="$2"
# an empty OUTDIR would make work="/work" and `rm -rf` it
case "$outdir" in "" | "/") die "OUTDIR must be a directory of its own, not '$outdir'" ;; esac
local update=0
case "${3:-}" in
"") ;;
--update) update=1 ;;
*) die "unknown argument '$3' (usage: $SMOKE_USAGE)" ;;
esac
[ $# -le 3 ] || die "too many arguments (usage: $SMOKE_USAGE)"
[ -x "$binary" ] || die "no executable $binary (build it first; see docs/BUILDING.md)"

local work="$outdir/work"
rm -rf "$work" || die "cannot remove $work"
mkdir -p "$work" || die "cannot create $work"

play "$binary" "$work" "$script" --rtc-fixed "$rtc_epoch"

if [ "$update" -eq 1 ]; then
    [ "$failures" -eq 0 ] || die "not updating the expectations from a failing run"
    rm -rf "$expected" || die "cannot remove $expected"
    mkdir -p "$expected" || die "cannot create $expected"
    local dump
    for dump in "$work"/*.txt; do
        [ -f "$dump" ] || die "the replay wrote no dump to $work"
        facts_of "$dump" > "$expected/$(basename "$dump")" || die "cannot write the expectation of $dump"
    done
    (cd "$work" && md5_list *.ppm) > "$expected/screenshots.md5" || die "cannot write the screenshot checksums"
    echo "updated $expected: $(ls "$expected"/*.txt | wc -l) dumps, $(wc -l < "$expected/screenshots.md5") screenshots"
    exit 0
fi

[ -d "$expected" ] || die "no $expected; run with --update once"

# Dumps.
local want got name
for want in "$expected"/*.txt; do
    name="$(basename "$want")"
    got="$work/$name"
    if [ ! -f "$got" ]; then
        fail "the replay did not write $name"
        continue
    fi
    if ! diff <(facts_of "$got") "$want" > "$work/$name.diff"; then
        fail "$name differs (got <, expected >):"
        cat "$work/$name.diff"
    else
        rm -f "$work/$name.diff"
    fi
done
for got in "$work"/*.txt; do
    [ -f "$expected/$(basename "$got")" ] || fail "$(basename "$got") is new (no expectation); run --update"
done

# Screenshots, by md5.
if ! diff <(cd "$work" && md5_list *.ppm) "$expected/screenshots.md5" > "$work/screenshots.diff"; then
    fail "screenshots differ (got <, expected >; the PPMs are in $work):"
    cat "$work/screenshots.diff"
else
    rm -f "$work/screenshots.diff"
fi

# The save round trips: what the game saved is what Continue loaded.
local trip before_dump after_dump keys
for trip in $SMOKE_ROUND_TRIP; do
    IFS=: read -r before_dump after_dump keys <<< "$trip"
    same_facts "$work/$before_dump.txt" "$work/$after_dump.txt" ${keys//,/ }
done

if [ "$failures" -eq 0 ]; then
    echo "PASS: $(ls "$expected"/*.txt | wc -l) dumps, $(wc -l < "$expected/screenshots.md5") screenshots, the save round trip"
    exit 0
fi
echo "FAILED: $failures problem(s); outputs are in $work"
exit 1
}
