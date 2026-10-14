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
#      other/  TO_BINARY on a copy whose header names layout id 2: refused at start (exit status
#              1, the refusal on stderr), the file unchanged.
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
mkdir -p "$work/from" "$work/to" "$work/bare" "$work/other" "$work/again" || die "cannot create $work"

# ---- 1. FROM: the opening up to and including the save ----------------------------------------
play_until_save "$from_binary" "$work/from"
resume_point "$work/from"

# ---- 2. TO: Continue on that save, as the restart would ----------------------------------------
cp "$work/from/pokeemerald.sav" "$work/from/pokeemerald.rtc" "$work/to/" || die "cannot copy the save"
continue_on "$to_binary" "$work/to" "$work/from"
grep -q "has a $from_bits-bit build's save layout; converted 1 save slot" "$work/to/stderr.log" \
    || fail "to: TO did not report converting the save from $from_bits bits"
cmp -s "$work/from/pokeemerald.sav" "$work/to/pokeemerald.sav" || fail "to: the save file changed, but the game did not save"

# ---- 3. TO on the bare image (a 32-bit save without its header) ---------------------------------
if [ "$from_bits" = 32 ]; then
    tail -c "$save_image_size" "$work/from/pokeemerald.sav" > "$work/bare/pokeemerald.sav" || die "cannot strip the header"
    cp "$work/from/pokeemerald.rtc" "$work/bare/" || die "cannot copy the RTC file"
    [ "$(($(file_size "$work/from/pokeemerald.sav") - save_header_size))" -eq "$save_image_size" ] \
        || fail "bare: the save file is not a header and a $save_image_size-byte image"
    continue_on "$to_binary" "$work/bare" "$work/from"
    grep -q "has no header" "$work/bare/stderr.log" || fail "bare: TO did not take the image for a bare one"
else
    echo "== bare: skipped (only a 32-bit save is ever bare)"
fi

# ---- 3b. TO refuses a save whose layout id it does not know -------------------------------------
refuse_other_layout "$to_binary" "$work/other" "$work/from/pokeemerald.sav"

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
