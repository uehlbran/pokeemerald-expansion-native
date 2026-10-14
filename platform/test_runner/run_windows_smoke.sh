#!/usr/bin/env bash
# run_windows_smoke.sh: the Windows executable's start-up, on Windows under
# Git Bash: started with --headless --silent --frames 60 in an empty scratch
# directory, it must map its regions, install its handlers, write the clock's
# offset file (host_file's durable write) and run 60 frames of the main loop:
#
#   platform/test_runner/run_windows_smoke.sh build/native64-windows/pokeemerald-native.exe
#
# Passes, exit status 0, only if the executable exits 0, its standard output
# is the one line "main: 60 frames", its standard error is empty (every module
# start that fails says so there), and pokeemerald.rtc holds exactly one decimal
# number and a "\n" with no "\r" (the file is written in binary mode). SDL2.dll
# must be beside the executable, as the build leaves it.
#
# Then the same from a directory whose name is not ASCII, with save and clock
# paths that are not ASCII either on the command line (relative, so that Git
# Bash passes them as they are): the UTF-8 code page of the executable's
# manifest (platform/build_glue/windows_manifest.xml) must carry them from
# argv to the files. It passes if the run passes as above with the clock's
# file under exactly its name and nothing else in the directory; and if a
# second run, with an 11-byte file without the save header at the save path,
# is refused (exit status 1) with standard error naming that path byte for
# byte, and the file is left as it was.
set -u
# Git Bash converts the arguments it passes a Windows program from its
# locale's character set: UTF-8.
export LC_ALL=C.UTF-8

FRAMES=60
LIMIT_SECONDS=120

if [ $# -ne 1 ] || [ ! -f "$1" ]; then
    echo "usage: run_windows_smoke.sh EXE" >&2
    exit 2
fi
exe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

failed=0
fail() {
    echo "FAILED: $*"
    failed=1
}

# run DIRECTORY ARGUMENT...: the executable started in DIRECTORY with
# --headless --silent --frames $FRAMES and the arguments; its exit status in
# $code, its output in $scratch/stdout and $scratch/stderr, both shown.
run() {
    local directory=$1
    shift
    (cd "$directory" && timeout $LIMIT_SECONDS "$exe" --headless --silent --frames $FRAMES "$@") \
        > "$scratch/stdout" 2> "$scratch/stderr"
    code=$?
    for stream in stdout stderr; do
        echo "-- $stream"
        cat "$scratch/$stream"
    done
}

# check_run RTC_FILE: the checks of a run that must pass.
check_run() {
    local rtc_file=$1
    [ $code -eq 0 ] || fail "exit status $code"
    [ "$(tr -d '\r' < "$scratch/stdout")" = "main: $FRAMES frames" ] || fail "standard output is not \"main: $FRAMES frames\""
    [ ! -s "$scratch/stderr" ] || fail "standard error is not empty"
    local rtc
    rtc=$(cat "$rtc_file" 2>/dev/null; printf x) # the x keeps the trailing newline
    local one_number=$'^-?[0-9]+\n$'
    [[ ${rtc%x} =~ $one_number ]] || fail "$rtc_file is not one number and a newline"
    ! grep -q $'\r' "$rtc_file" 2>/dev/null || fail "$rtc_file has a \\r (not written in binary mode)"
}

echo "== in an empty directory"
run "$scratch"
check_run "$scratch/pokeemerald.rtc"

# "Unicode-" with accented letters and "Nihon" in Kanji; "seebu.sav" in
# Katakana; "tokei.rtc" (clock) in Kanji. Written as UTF-8 bytes, so that
# this script is ASCII.
unicode_dir="$scratch/"$'\xc3\x9cn\xc3\xafc\xc3\xb6d\xc3\xa9-\xe6\x97\xa5\xe6\x9c\xac'
save_name=$'\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96.sav'
rtc_name=$'\xe6\x99\x82\xe8\xa8\x88.rtc'
mkdir "$unicode_dir" || fail "cannot make the non-ASCII directory"

echo "== in a non-ASCII directory, with non-ASCII save and clock paths"
run "$unicode_dir" --save "$save_name" --rtc-offset "$rtc_name"
check_run "$unicode_dir/$rtc_name"
# The directory may hold only the clock file, the save's lock (platform/save_lock.c
# creates <save>.lock before the modules start and never deletes it) and the save.
listed=$(cd "$unicode_dir" && ls -A)
lock_name="$save_name.lock"
[ -e "$unicode_dir/$lock_name" ] || fail "the non-ASCII directory has no $lock_name"
while IFS= read -r name; do
    case $name in
    "$save_name" | "$rtc_name" | "$lock_name") ;;
    *) fail "the non-ASCII directory holds another name: $name" ;;
    esac
done <<< "$listed"

echo "== the same with an 11-byte file without the save header at the non-ASCII save path"
printf 'not a save\n' > "$unicode_dir/$save_name"
cp -- "$unicode_dir/$save_name" "$scratch/not_a_save.before" || fail "cannot copy the file at the save path"
run "$unicode_dir" --save "$save_name" --rtc-offset "$rtc_name"
[ $code -eq 1 ] || fail "exit status $code, not 1, for a file that is not a save"
# The refusal's fixed start (platform/save_file.c, refuse_headerless).
grep -qF -- "save_file: $save_name: has no save header" "$scratch/stderr" \
    || fail "standard error does not name the save path as it was given in the refusal"
cmp -s "$scratch/not_a_save.before" "$unicode_dir/$save_name" || fail "the refused file at the save path changed"

if [ $failed -ne 0 ]; then
    exit 1
fi
echo "PASS: the Windows executable started, ran $FRAMES frames and wrote its clock's offset, in an ASCII and in a non-ASCII directory, and read its save through a non-ASCII path"
