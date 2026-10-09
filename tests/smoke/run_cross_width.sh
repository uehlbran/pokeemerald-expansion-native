#!/usr/bin/env bash
# tests/smoke/run_cross_width.sh: gate (h), a save carried between the 32-bit and the 64-bit build.
#
#   tests/smoke/run_cross_width.sh FROM_BINARY TO_BINARY OUTDIR    exit 0 if every check passes
#
# FROM_BINARY plays opening.replay up to and including the save and stops where the script's
# `restart` is; TO_BINARY, a build of the other width, then starts on that pokeemerald.sav as the
# restart would (`--resume-at` the `restart` line, the clock advanced the same way) and plays the
# rest: Continue. Builds nothing; README.md has the commands. Steps and checks:
#
#   1. from/   FROM_BINARY, a copy of opening.replay cut before its `restart`: exit 0, no new
#              sanitizer report, every dump and screenshot as in expected/ (the save is the one
#              run.sh checks), and a save file whose header names FROM's width and layout 1.
#   2. to/     TO_BINARY on a copy of that file, opening.replay --resume-at the `restart` line:
#              the same, for the dumps and screenshots after the restart (26_continue_menu,
#              27_continued); what was saved (25_saved) is what Continue loaded; stderr says the
#              save was converted; the file is unchanged (the conversion is not a game write).
#   3. bare/   only when FROM is 32-bit: step 2 on the image without its header (what an
#              emulator writes), which the 64-bit build takes to have the 32-bit layout.
#   4. again/  TO_BINARY on another copy, cross_width_save.replay: Continue, save again, restart,
#              Continue (the replay's own `expect`s); the header then names TO's width, not FROM's,
#              and both saves show the scene 27_continued shows.
# Run the sanitized builds with ASAN_OPTIONS=halt_on_error=0 (lib.sh).
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMOKE_NAME=run_cross_width.sh
. "$here/lib.sh"

usage="run_cross_width.sh FROM_BINARY TO_BINARY OUTDIR"
[ $# -eq 3 ] || die "usage: $usage"
from_binary="$1"
to_binary="$2"
outdir="$3"
case "$outdir" in "" | "/") die "OUTDIR must be a directory of its own, not '$outdir'" ;; esac
[ -x "$from_binary" ] || die "no executable $from_binary (build it first; see docs/BUILDING.md)"
[ -x "$to_binary" ] || die "no executable $to_binary (build it first; see docs/BUILDING.md)"

opening="$here/opening.replay"
expected="$here/expected"
work="$outdir/work"
rm -rf "$work" || die "cannot remove $work"
mkdir -p "$work/from" "$work/to" "$work/bare" "$work/again" || die "cannot create $work"

# platform/save_file.h: the 8-byte magic, then version, pointer width and layout id, u32 LE.
save_header_magic="PENSAVE"
save_header_size=28
save_image_size=131072
# platform/fixed_clock.h: the clock advances one second every 60 frames; restart.c advances
# --rtc-fixed by the seconds the run took.
fixed_clock_frames_per_second=60

failures=0
fail() {
    echo "FAIL: $*"
    failures=$((failures + 1))
}

# "BITS LAYOUT" of a save file with a header, or "none".
save_layout() {
    if [ "$(head -c ${#save_header_magic} "$1")" != "$save_header_magic" ]; then
        echo none
        return
    fi
    od -A n -t u4 -j 12 -N 8 "$1" | tr -s ' ' | sed 's/^ //'
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
    if [ "$status" -ne 0 ]; then
        fail "$(basename "$dir"): the replay ended with status $status:"
        grep -E '^(replay|main|restart|symbols|save_file):' "$dir/stderr.log" | head -20
    fi
    if new_sanitizer_reports "$dir/stderr.log" "$dir/sanitizer-new.log"; then
        fail "$(basename "$dir"): sanitizer reports in $dir/stderr.log:"
        sort "$dir/sanitizer-new.log" | uniq -c | sort -rn | head -40
    fi
}

# Every dump and screenshot in directory $1 against expected/; the names it must have written
# follow as arguments.
compare_with_expected() {
    local dir="$1" got name
    shift
    for name in "$@"; do
        [ -f "$dir/$name.txt" ] || fail "$(basename "$dir"): the replay did not write $name.txt"
    done
    for got in "$dir"/*.txt; do
        [ -f "$got" ] || continue
        name="$(basename "$got")"
        if [ ! -f "$expected/$name" ]; then
            fail "$(basename "$dir"): $name has no expectation in $expected"
        elif ! diff <(facts_of "$got") "$expected/$name" > "$got.diff"; then
            fail "$(basename "$dir"): $name differs (got <, expected >):"
            cat "$got.diff"
        else
            rm -f "$got.diff"
        fi
    done
    local shot want
    for shot in "$dir"/*.ppm; do
        [ -f "$shot" ] || continue
        name="$(basename "$shot")"
        want="$(grep_or_die " $name\$" "$expected/screenshots.md5" | cut -d' ' -f1)"
        [ -n "$want" ] || { fail "$(basename "$dir"): $name has no expected checksum"; continue; }
        [ "$(md5sum < "$shot" | cut -d' ' -f1)" = "$want" ] || fail "$(basename "$dir"): $name differs from expected/screenshots.md5"
    done
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

# ---- 1. FROM: the opening up to and including the save ----------------------------------------
restart_line="$(grep -n -x 'restart' "$opening" | head -1 | cut -d: -f1)"
[ -n "$restart_line" ] || die "$opening has no restart"
head -n "$((restart_line - 1))" "$opening" > "$work/from/until_save.replay" || die "cannot cut $opening"
for include in $(sed -n -E 's/^include[[:space:]]+([^[:space:]#]+).*/\1/p' "$work/from/until_save.replay"); do
    cp "$here/$include" "$work/from/" || die "cannot copy $include"
done
play "$from_binary" "$work/from" "$work/from/until_save.replay" --rtc-fixed "$rtc_epoch"
compare_with_expected "$work/from" 25_saved
from_layout="$(save_layout "$work/from/pokeemerald.sav")"
case "$from_layout" in
"32 1" | "64 1") echo "FROM saved with layout tag {${from_layout/ /,}}" ;;
*) fail "the save FROM wrote has the layout tag '$from_layout', not {32,1} or {64,1}" ;;
esac
from_bits="${from_layout%% *}"

# Where FROM stopped is where `restart` would have restarted: the line after it in the script with
# its includes expanded (the expansion of the cut copy ends just before it), and the clock moved on.
resume_at="$(($(wc -l < "$work/from/expanded.replay") + 1))"
frames="$(sed -n -E 's/^main: ([0-9]+) frames$/\1/p' "$work/from/stdout.log" | tail -1)"
[ -n "$frames" ] || die "FROM did not report its frame count"
epoch="$((rtc_epoch + frames / fixed_clock_frames_per_second))"

# ---- 2. TO: Continue on that save, as the restart would ----------------------------------------
continue_on() {
    local dir="$1"
    play "$to_binary" "$dir" "$opening" --rtc-fixed "$epoch" --resume-at "$resume_at"
    [ "$(sed -n "${resume_at}p" "$dir/expanded.replay")" = restart ] \
        || fail "$(basename "$dir"): line $resume_at of the expanded opening.replay is not its restart"
    compare_with_expected "$dir" 26_continue_menu 27_continued
    same_facts "$work/from/25_saved.txt" "$dir/27_continued.txt" player_name map party0_species party_count
}

cp "$work/from/pokeemerald.sav" "$work/from/pokeemerald.rtc" "$work/to/" || die "cannot copy the save"
continue_on "$work/to"
grep -q "has a $from_bits-bit build's save layout; converted 1 save slot" "$work/to/stderr.log" \
    || fail "to: TO did not report converting the save from $from_bits bits"
cmp -s "$work/from/pokeemerald.sav" "$work/to/pokeemerald.sav" || fail "to: the save file changed, but the game did not save"

# ---- 3. TO on the bare image (a 32-bit save without its header) ---------------------------------
if [ "$from_bits" = 32 ]; then
    tail -c "$save_image_size" "$work/from/pokeemerald.sav" > "$work/bare/pokeemerald.sav" || die "cannot strip the header"
    cp "$work/from/pokeemerald.rtc" "$work/bare/" || die "cannot copy the RTC file"
    [ "$(($(stat -c %s "$work/from/pokeemerald.sav") - save_header_size))" -eq "$save_image_size" ] \
        || fail "bare: the save file is not a header and a $save_image_size-byte image"
    continue_on "$work/bare"
    grep -q "has no header" "$work/bare/stderr.log" || fail "bare: TO did not take the image for a bare one"
else
    echo "== bare: skipped (only a 32-bit save is ever bare)"
fi

# ---- 4. TO saves in its own layout and comes up from it -----------------------------------------
cp "$work/from/pokeemerald.sav" "$work/from/pokeemerald.rtc" "$work/again/" || die "cannot copy the save"
play "$to_binary" "$work/again" "$here/cross_width_save.replay" --rtc-fixed "$epoch"
to_layout="$(save_layout "$work/again/pokeemerald.sav")"
case "$to_layout" in
"32 1" | "64 1") ;;
*) fail "again: the save TO wrote has the layout tag '$to_layout', not {32,1} or {64,1}" ;;
esac
[ "${to_layout%% *}" != "$from_bits" ] || fail "again: TO saved with FROM's width, $from_bits: not a cross-width run"
echo "TO saved with layout tag {${to_layout/ /,}}"
same_facts "$work/again/28_saved_again.txt" "$work/again/29_continued_again.txt" \
    player_name map party0_species party_count save_counter
continued_md5="$(grep_or_die ' 27_continued.ppm$' "$expected/screenshots.md5" | cut -d' ' -f1)"
for shot in 28_saved_again 29_continued_again; do
    [ "$(md5sum < "$work/again/$shot.ppm" 2> /dev/null | cut -d' ' -f1)" = "$continued_md5" ] \
        || fail "again: $shot.ppm is not the scene of 27_continued.ppm"
done

# Between them, FROM and TO wrote every dump and screenshot of expected/.
for want in "$expected"/*.txt; do
    name="$(basename "$want")"
    [ -f "$work/from/$name" ] || [ -f "$work/to/$name" ] || fail "neither FROM nor TO wrote $name"
done
while read -r _ name; do
    [ -f "$work/from/$name" ] || [ -f "$work/to/$name" ] || fail "neither FROM nor TO wrote $name"
done < "$expected/screenshots.md5"

if [ "$failures" -eq 0 ]; then
    echo "PASS: a {${from_layout/ /,}} save continued, and saved again as {${to_layout/ /,}}, by $to_binary"
    exit 0
fi
echo "FAILED: $failures problem(s); outputs are in $work"
exit 1
