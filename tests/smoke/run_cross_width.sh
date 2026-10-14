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
#   3. headerless/  TO_BINARY on the image without its header (what an emulator writes): refused
#              at start (exit status 1, "has no save header" on stderr), the file unchanged.
#      other/  TO_BINARY on a copy whose header names layout id 2: refused at start (exit status
#              1, the refusal on stderr), the file unchanged.
#   3c. busy/  TO_BINARY on a copy FROM_BINARY is playing (started first, waited for until it holds
#              the save's lock, /proc/locks): refused at start (exit status 1, one line on stderr
#              naming the save and another instance), the file unchanged; FROM killed with SIGKILL,
#              as a crash ends it, TO then starts on the file (exit status 0).
#   4. again/  TO_BINARY on another copy, cross_width_save.replay: Continue, save again, restart,
#              Continue (the replay's own `expect`s); the header then names TO's width, not FROM's,
#              and both saves show the scene 27_continued shows.
# Run the sanitized builds with ASAN_OPTIONS=halt_on_error=0 (lib.sh).
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMOKE_NAME=run_cross_width.sh
. "$here/lib.sh"
. "$here/save_steps.sh"

usage="run_cross_width.sh FROM_BINARY TO_BINARY OUTDIR"
[ $# -eq 3 ] || die "usage: $usage"
from_binary="$1"
to_binary="$2"
outdir="$3"
case "$outdir" in "" | "/") die "OUTDIR must be a directory of its own, not '$outdir'" ;; esac
[ -x "$from_binary" ] || die "no executable $from_binary (build it first; see docs/BUILDING.md)"
[ -x "$to_binary" ] || die "no executable $to_binary (build it first; see docs/BUILDING.md)"

work="$outdir/work"
rm -rf "$work" || die "cannot remove $work"
mkdir -p "$work/from" "$work/to" "$work/headerless" "$work/other" "$work/busy" "$work/again" || die "cannot create $work"

# ---- 1. FROM: the opening up to and including the save ----------------------------------------
play_until_save "$from_binary" "$work/from"
resume_point "$work/from"

# ---- 2. TO: Continue on that save, as the restart would ----------------------------------------
cp "$work/from/pokeemerald.sav" "$work/from/pokeemerald.rtc" "$work/to/" || die "cannot copy the save"
continue_on "$to_binary" "$work/to" "$work/from"
grep -q "has a $from_bits-bit build's save layout; converted 1 save slot" "$work/to/stderr.log" \
    || fail "to: TO did not report converting the save from $from_bits bits"
cmp -s "$work/from/pokeemerald.sav" "$work/to/pokeemerald.sav" || fail "to: the save file changed, but the game did not save"

# ---- 3. TO refuses the image without its header ------------------------------------------------
# Not converted or taken for any layout: the file looks like an emulator's save (save_file.h).
tail -c "$save_image_size" "$work/from/pokeemerald.sav" > "$work/headerless/pokeemerald.sav" || die "cannot strip the header"
[ "$(($(file_size "$work/from/pokeemerald.sav") - save_header_size))" -eq "$save_image_size" ] \
    || fail "headerless: the save file is not a header and a $save_image_size-byte image"
cp "$work/headerless/pokeemerald.sav" "$work/headerless/before.sav" || die "cannot copy the save"
echo "== headerless: $to_binary on a save without its header"
"$to_binary" --headless --silent --unthrottled --frames 60 --save "$work/headerless/pokeemerald.sav" \
    --rtc-offset "$work/headerless/pokeemerald.rtc" > "$work/headerless/stdout.log" 2> "$work/headerless/stderr.log"
status=$?
[ "$status" -eq "$main_exit_failure" ] || fail "headerless: TO ended with status $status, not $main_exit_failure"
grep -q "has no save header, so it looks like an emulator or cartridge save" "$work/headerless/stderr.log" \
    || fail "headerless: TO did not report refusing the file"
cmp -s "$work/headerless/before.sav" "$work/headerless/pokeemerald.sav" || fail "headerless: the refused save file changed"
if new_sanitizer_reports "$work/headerless/stderr.log" "$work/headerless/sanitizer-new.log"; then
    fail "headerless: sanitizer reports in $work/headerless/stderr.log"
fi

# ---- 3b. TO refuses a save whose layout id it does not know -------------------------------------
refuse_other_layout "$to_binary" "$work/other" "$work/from/pokeemerald.sav"

# ---- 3c. TO refuses a save another instance is playing, until that instance is gone --------------
# The lock is flock(2)'s on <save>.lock (platform/save_lock.h). Linux lists every lock held in
# /proc/locks by device and inode, which shows that FROM holds it without the check taking it (a
# check that took it could be what refused FROM).
holds_save_lock() {
    local inode
    inode="$(stat -c %i "$1.lock" 2> /dev/null)" || return 1
    grep -qE "^[0-9]+: FLOCK +ADVISORY +WRITE +[0-9]+ [0-9a-f]+:[0-9a-f]+:$inode " /proc/locks
}
busy="$work/busy"
cp "$work/from/pokeemerald.sav" "$busy/pokeemerald.sav" && cp "$busy/pokeemerald.sav" "$busy/before.sav" \
    || die "cannot copy the save"
echo "== busy: $to_binary on a save $from_binary is playing"
# Bounded, should the kill below never come: about a minute of the intro, unthrottled.
"$from_binary" --headless --silent --unthrottled --frames 50000 --save "$busy/pokeemerald.sav" \
    --rtc-offset "$busy/pokeemerald.rtc" > "$busy/holder.stdout.log" 2> "$busy/holder.stderr.log" &
holder=$!
for _ in $(seq 100); do
    holds_save_lock "$busy/pokeemerald.sav" && break
    sleep 0.1
done
holds_save_lock "$busy/pokeemerald.sav" || fail "busy: FROM did not take the save's lock within 10 seconds"
"$to_binary" --headless --silent --unthrottled --frames 60 --save "$busy/pokeemerald.sav" \
    --rtc-offset "$busy/pokeemerald.rtc" > "$busy/stdout.log" 2> "$busy/stderr.log"
status=$?
kill -KILL "$holder" 2> /dev/null
wait "$holder" 2> /dev/null # the shell's "Killed" notice: the status says it
holder_status=$?
[ "$status" -eq "$main_exit_failure" ] || fail "busy: TO ended with status $status, not $main_exit_failure"
[ "$(wc -l < "$busy/stderr.log")" -eq 1 ] \
    && grep -qF "save_lock: another instance of the game holds the save $busy/pokeemerald.sav " "$busy/stderr.log" \
    || fail "busy: TO's stderr is not the one line that another instance holds the save"
[ "$holder_status" -eq $((128 + 9)) ] || fail "busy: FROM ended with status $holder_status, not by the SIGKILL"
"$to_binary" --headless --silent --unthrottled --frames 60 --save "$busy/pokeemerald.sav" \
    --rtc-offset "$busy/pokeemerald.rtc" > "$busy/after.stdout.log" 2> "$busy/after.stderr.log"
status=$?
[ "$status" -eq 0 ] || fail "busy: TO ended with status $status once FROM was killed, not 0 (see $busy/after.stderr.log)"
cmp -s "$busy/before.sav" "$busy/pokeemerald.sav" || fail "busy: the save file changed, but no game saved"
for log in stderr after.stderr; do
    if new_sanitizer_reports "$busy/$log.log" "$busy/$log.sanitizer-new.log"; then
        fail "busy: sanitizer reports in $busy/$log.log"
    fi
done

# ---- 4. TO saves in its own layout and comes up from it -----------------------------------------
save_again "$to_binary" "$work/again" "$work/from"
[ "${to_layout%% *}" != "$from_bits" ] || fail "again: TO saved with FROM's width, $from_bits: not a cross-width run"
echo "TO saved with layout tag {${to_layout/ /,}}"
check_saved_again "$work/again"

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
