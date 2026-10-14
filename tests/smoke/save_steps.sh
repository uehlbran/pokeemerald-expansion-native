# tests/smoke/save_steps.sh: the steps of gate (h), a save carried from one
# build to another, shared by run_cross_width.sh (between the 32-bit and the
# 64-bit build) and run_cross_os.sh (between the Linux and the Windows 64-bit
# build). Sourced after lib.sh, not run. Each step plays in a directory of
# its own and counts its failed checks in lib.sh's `failures`.

opening="$here/opening.replay"
expected="$here/expected"

# platform/save_file.h: the 8-byte magic, then version, pointer width and layout id, u32 LE.
save_header_magic="PENSAVE"
save_header_size=28
save_header_layout_id_at=16
# main.c's exit status when a module cannot start, a refused save among them.
main_exit_failure=1
save_image_size=131072
# platform/fixed_clock.h: the clock advances one second every 60 frames; restart.c advances
# --rtc-fixed by the seconds the run took.
fixed_clock_frames_per_second=60
fixed_clock_header="$here/../../platform/fixed_clock.h"

# "BITS LAYOUT" of a save file with a header, or "none".
save_layout() {
    if [ "$(head -c ${#save_header_magic} "$1")" != "$save_header_magic" ]; then
        echo none
        return
    fi
    od -A n -t u4 -j 12 -N 8 "$1" | tr -s ' ' | sed 's/^ //'
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
        [ "$(md5_of "$shot")" = "$want" ] || fail "$(basename "$dir"): $name differs from expected/screenshots.md5"
    done
}

# Step 1: binary $1 plays opening.replay up to and including the save, in directory $2 (a copy of
# the script cut before its `restart`), and stops where the `restart` is. Sets from_layout ("BITS
# LAYOUT" of the save it wrote) and from_bits.
play_until_save() {
    local binary="$1" dir="$2" restart_line include
    restart_line="$(grep -n -x 'restart' "$opening" | head -1 | cut -d: -f1)"
    [ -n "$restart_line" ] || die "$opening has no restart"
    head -n "$((restart_line - 1))" "$opening" > "$dir/until_save.replay" || die "cannot cut $opening"
    for include in $(sed -n -E 's/^include[[:space:]]+([^[:space:]#]+).*/\1/p' "$dir/until_save.replay"); do
        cp "$here/$include" "$dir/" || die "cannot copy $include"
    done
    play "$binary" "$dir" "$dir/until_save.replay" --rtc-fixed "$rtc_epoch"
    compare_with_expected "$dir" 25_saved
    from_layout="$(save_layout "$dir/pokeemerald.sav")"
    case "$from_layout" in
    "32 1" | "64 1") echo "FROM saved with layout tag {${from_layout/ /,}}" ;;
    *) fail "the save FROM wrote has the layout tag '$from_layout', not {32,1} or {64,1}" ;;
    esac
    from_bits="${from_layout%% *}"
}

# Where step 1 in directory $1 stopped is where `restart` would have restarted: the line after it
# in the script with its includes expanded (the expansion of the cut copy ends just before it), and
# the clock moved on. Sets resume_at and epoch.
resume_point() {
    local dir="$1"
    resume_at="$(($(wc -l < "$dir/expanded.replay") + 1))"
    read_frames_played "$dir"
    epoch="$((rtc_epoch + frames_played / fixed_clock_frames_per_second))"
}

# Sets frames_played to the frame count the run in directory $1 reported on standard output.
read_frames_played() {
    frames_played="$(tr -d '\r' < "$1/stdout.log" | sed -n -E 's/^main: ([0-9]+) frames$/\1/p' | tail -1)"
    [ -n "$frames_played" ] || die "$(basename "$1") did not report its frame count"
}

# The clock resume_point set is the one the restart would have set after the run in directory $1
# (main.c: fixed_clock_at, the base plus the run's frames in whole seconds). No dump shows the
# clock, so a wrong one would pass every other check of the gate. The rate is read from the
# header here, which also keeps fixed_clock_frames_per_second in step with it.
check_resume_epoch() {
    local from_dir="$1" per_second want
    per_second="$(sed -n -E 's/^#define FIXED_CLOCK_FRAMES_PER_SECOND ([0-9]+)u?$/\1/p' "$fixed_clock_header")"
    [ -n "$per_second" ] || die "no FIXED_CLOCK_FRAMES_PER_SECOND in $fixed_clock_header"
    read_frames_played "$from_dir"
    want="$((rtc_epoch + frames_played / per_second))"
    [ "$epoch" = "$want" ] \
        || fail "the clock after the restart is $epoch, not $want ($rtc_epoch + $frames_played frames / $per_second)"
}

# Step 2: binary $1 in directory $2, on the save step 1 wrote in directory $3 (copied there), plays
# opening.replay from resume_point's line and clock, as the restart would: Continue.
continue_on() {
    local binary="$1" dir="$2" from_dir="$3"
    check_resume_epoch "$from_dir"
    play "$binary" "$dir" "$opening" --rtc-fixed "$epoch" --resume-at "$resume_at"
    [ "$(sed -n "${resume_at}p" "$dir/expanded.replay")" = restart ] \
        || fail "$(basename "$dir"): line $resume_at of the expanded opening.replay is not its restart"
    compare_with_expected "$dir" 26_continue_menu 27_continued
    same_facts "$from_dir/25_saved.txt" "$dir/27_continued.txt" player_name map party0_species party_count
}

# Step 3b: binary $1 in directory $2 on a copy of save $3 whose header names a layout id it does not
# know: refused at start. The header is not under the CRC, so only the id changes. A run that is not
# refused stops after a few frames and fails the status check.
refuse_other_layout() {
    local binary="$1" dir="$2" save="$3" status
    cp "$save" "$dir/" || die "cannot copy the save"
    printf '\002' | dd of="$dir/pokeemerald.sav" bs=1 seek="$save_header_layout_id_at" conv=notrunc status=none \
        || die "cannot patch the layout id"
    cp "$dir/pokeemerald.sav" "$dir/before.sav" || die "cannot copy the save"
    echo "== other: $binary on a save with layout id 2"
    "$binary" --headless --silent --unthrottled --frames 60 --save "$dir/pokeemerald.sav" \
        --rtc-offset "$dir/pokeemerald.rtc" > "$dir/stdout.log" 2> "$dir/stderr.log"
    status=$?
    [ "$status" -eq "$main_exit_failure" ] || fail "other: TO ended with status $status, not $main_exit_failure"
    grep -q "was written by a build with another save layout" "$dir/stderr.log" \
        || fail "other: TO did not report refusing layout id 2"
    cmp -s "$dir/before.sav" "$dir/pokeemerald.sav" || fail "other: the refused save file changed"
    if new_sanitizer_reports "$dir/stderr.log" "$dir/sanitizer-new.log"; then
        fail "other: sanitizer reports in $dir/stderr.log"
    fi
}

# Step 4: binary $1 in directory $2, on the save and clock step 1 wrote in directory $3 (copied
# there), plays cross_width_save.replay: Continue, save again in its own layout, restart, Continue.
# Sets to_layout ("BITS LAYOUT" of the save it wrote).
save_again() {
    local binary="$1" dir="$2" from_dir="$3"
    cp "$from_dir/pokeemerald.sav" "$from_dir/pokeemerald.rtc" "$dir/" || die "cannot copy the save"
    play "$binary" "$dir" "$here/cross_width_save.replay" --rtc-fixed "$epoch"
    to_layout="$(save_layout "$dir/pokeemerald.sav")"
    case "$to_layout" in
    "32 1" | "64 1") ;;
    *) fail "again: the save TO wrote has the layout tag '$to_layout', not {32,1} or {64,1}" ;;
    esac
}

# After step 4 in directory $1: what it saved is what it loaded, and both saves show the scene
# 27_continued shows.
check_saved_again() {
    local dir="$1" continued_md5 shot
    same_facts "$dir/28_saved_again.txt" "$dir/29_continued_again.txt" \
        player_name map party0_species party_count save_counter
    continued_md5="$(grep_or_die ' 27_continued.ppm$' "$expected/screenshots.md5" | cut -d' ' -f1)"
    for shot in 28_saved_again 29_continued_again; do
        [ "$(md5_of "$dir/$shot.ppm" 2> /dev/null)" = "$continued_md5" ] \
            || fail "again: $shot.ppm is not the scene of 27_continued.ppm"
    done
}
