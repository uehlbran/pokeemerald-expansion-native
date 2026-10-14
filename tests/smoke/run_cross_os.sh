#!/usr/bin/env bash
# tests/smoke/run_cross_os.sh: gate (h) across operating systems, a save carried from the Linux
# 64-bit build to the Windows one and back. The two lay the save out alike (the same widths;
# MinGW's bit-fields as GCC's on Linux, -mno-ms-bitfields, platform/target.mk), so nothing is
# converted. Three stages on two machines, each with the directories of the stage before it
# (.github/workflows/windows.yml carries them as artifacts):
#
#   run_cross_os.sh save LINUX_BINARY OUTDIR
#       on Linux: run_cross_width.sh's step 1 in OUTDIR/work/from: opening.replay up to and
#       including the save; every dump and screenshot as in expected/; the save's header names
#       {64,1}.
#   run_cross_os.sh load WINDOWS_BINARY FROM_DIR OUTDIR
#       on Windows (Git Bash), on that save: step 2 in OUTDIR/work/to: Continue as the restart
#       would, the dumps and screenshots after the restart as in expected/, what was saved is what
#       Continue loaded, nothing reported about the save (not foreign, nothing converted), the file
#       unchanged; step 3b in other/: a save with layout id 2 refused; step 4 in again/:
#       cross_width_save.replay saves again (in {64,1}), restarts and continues.
#   run_cross_os.sh back LINUX_BINARY FROM_DIR AGAIN_DIR OUTDIR
#       on Linux, on the save the Windows build wrote in again/, in OUTDIR/work/back:
#       cross_os_back.replay continues; what the Windows build saved (28_saved_again) is what
#       Continue loaded (30_continued_back), on the scene of 27_continued, with nothing reported
#       about the save and the file unchanged.
#
# Builds nothing; exit 0 if every check of the stage passes.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMOKE_NAME=run_cross_os.sh
. "$here/lib.sh"
. "$here/save_steps.sh"

usage="run_cross_os.sh save LINUX_BINARY OUTDIR | load WINDOWS_BINARY FROM_DIR OUTDIR | back LINUX_BINARY FROM_DIR AGAIN_DIR OUTDIR"
# The layout both builds write: 64-bit pointers, layout 1.
layout="64 1"

# Standard error $1 says nothing about the save file (save_file.c reports a foreign or converted
# save there); the directory's name is $2.
nothing_said_about_the_save() {
    if grep -q -e '^save_file:' -e 'save layout; converted' "$1"; then
        fail "$2: the save was reported as another build's:"
        grep -e '^save_file:' -e 'save layout; converted' "$1"
    fi
}

# The work directory under OUTDIR $1, fresh, with the subdirectories that follow.
fresh_work() {
    local outdir="$1"
    shift
    case "$outdir" in "" | "/") die "OUTDIR must be a directory of its own, not '$outdir'" ;; esac
    work="$outdir/work"
    rm -rf "$work" || die "cannot remove $work"
    local name
    for name; do
        mkdir -p "$work/$name" || die "cannot create $work/$name"
    done
}

stage_save() {
    [ $# -eq 2 ] || die "usage: $usage"
    local binary="$1"
    [ -x "$binary" ] || die "no executable $binary (build it first; see docs/BUILDING.md)"
    fresh_work "$2" from
    play_until_save "$binary" "$work/from"
    [ "$from_layout" = "$layout" ] || fail "from: the save has the layout tag {${from_layout/ /,}}, not {${layout/ /,}}"
    finish "a {${from_layout/ /,}} save written by $binary in $work/from"
}

stage_load() {
    [ $# -eq 3 ] || die "usage: $usage"
    local binary="$1" from_dir="$2"
    [ -x "$binary" ] || die "no executable $binary (build it first; see docs/BUILDING.md)"
    [ -f "$from_dir/pokeemerald.sav" ] || die "no $from_dir/pokeemerald.sav (run the save stage first)"
    fresh_work "$3" to other again
    resume_point "$from_dir"

    cp "$from_dir/pokeemerald.sav" "$from_dir/pokeemerald.rtc" "$work/to/" || die "cannot copy the save"
    continue_on "$binary" "$work/to" "$from_dir"
    nothing_said_about_the_save "$work/to/stderr.log" to
    cmp -s "$from_dir/pokeemerald.sav" "$work/to/pokeemerald.sav" || fail "to: the save file changed, but the game did not save"

    refuse_other_layout "$binary" "$work/other" "$from_dir/pokeemerald.sav"

    save_again "$binary" "$work/again" "$from_dir"
    [ "$to_layout" = "$layout" ] || fail "again: the save has the layout tag {${to_layout/ /,}}, not {${layout/ /,}}"
    nothing_said_about_the_save "$work/again/stderr.log" again
    check_saved_again "$work/again"
    finish "the save continued, and saved again as {${to_layout/ /,}}, by $binary in $work/again"
}

stage_back() {
    [ $# -eq 4 ] || die "usage: $usage"
    local binary="$1" from_dir="$2" again_dir="$3" continued_md5
    [ -x "$binary" ] || die "no executable $binary (build it first; see docs/BUILDING.md)"
    [ -f "$again_dir/pokeemerald.sav" ] || die "no $again_dir/pokeemerald.sav (run the load stage first)"
    fresh_work "$4" back
    resume_point "$from_dir"
    [ "$(save_layout "$again_dir/pokeemerald.sav")" = "$layout" ] \
        || fail "back: the save from $again_dir does not have the layout tag {${layout/ /,}}"
    cp "$again_dir/pokeemerald.sav" "$again_dir/pokeemerald.rtc" "$work/back/" || die "cannot copy the save"
    play "$binary" "$work/back" "$here/cross_os_back.replay" --rtc-fixed "$epoch"
    nothing_said_about_the_save "$work/back/stderr.log" back
    cmp -s "$again_dir/pokeemerald.sav" "$work/back/pokeemerald.sav" || fail "back: the save file changed, but the game did not save"
    same_facts "$again_dir/28_saved_again.txt" "$work/back/30_continued_back.txt" \
        player_name map party0_species party_count save_counter
    continued_md5="$(grep_or_die ' 27_continued.ppm$' "$expected/screenshots.md5" | cut -d' ' -f1)"
    [ "$(md5_of "$work/back/30_continued_back.ppm" 2> /dev/null)" = "$continued_md5" ] \
        || fail "back: 30_continued_back.ppm is not the scene of 27_continued.ppm"
    finish "the save $again_dir holds continued by $binary"
}

# The stage's verdict: PASS with $1, or the count of failures.
finish() {
    if [ "$failures" -eq 0 ]; then
        echo "PASS: $1"
        exit 0
    fi
    echo "FAILED: $failures problem(s); outputs are in $work"
    exit 1
}

[ $# -ge 1 ] || die "usage: $usage"
stage="$1"
shift
case "$stage" in
save) stage_save "$@" ;;
load) stage_load "$@" ;;
back) stage_back "$@" ;;
*) die "unknown stage '$stage' (usage: $usage)" ;;
esac
