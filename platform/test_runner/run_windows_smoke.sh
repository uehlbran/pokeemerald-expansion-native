#!/usr/bin/env bash
# run_windows_smoke.sh: the Windows executable's start-up, on Windows under
# Git Bash: started with --headless --silent --frames 60 in an empty scratch
# directory and --data-dir data, it must map its regions, install its
# handlers, write the clock's offset file (host_file's durable write) into the
# data directory it made and run 60 frames of the main loop:
#
#   platform/test_runner/run_windows_smoke.sh build/native64-windows/pokeemerald-native.exe
#
# Passes, exit status 0, only if the executable exits 0, its standard output
# is the one line "main: 60 frames", its standard error is empty (every module
# start that fails says so there), and data/pokeemerald.rtc holds exactly one
# decimal number and a "\n" with no "\r" (the file is written in binary mode)
# beside the save's lock, with nothing else written in the working directory.
# SDL2.dll must be beside the executable, as the build leaves it.
#
# Then the same from a directory whose name is not ASCII, with save and clock
# paths that are not ASCII either on the command line (relative, so that Git
# Bash passes them as they are): the UTF-8 code page of the executable's
# manifest (platform/build_glue/windows_manifest.xml) must carry them from
# argv to the files. It passes if the run passes as above with the clock's
# file under exactly its name and nothing else in the directory; and if a
# second run, with an 11-byte file without the save header at the save path,
# is refused (exit status 1) with standard error naming that path byte for
# byte, and the file is left as it was. A third run there names a data
# directory that is not ASCII (--data-dir), which must be made and get the
# clock's file and the save's lock.
#
# Then the player's files as --keys prints them (docs/RUNNING.md): with
# --data-dir, the four paths in it, the default keys, and, with a settings.ini
# there, its key and its bad line reported by number; --version, the version
# alone, the one --keys printed first; and portable
# mode, a copy of the executable (and SDL2.dll) beside a settings.ini, whose
# --keys names the copy's directory as SDL_GetBasePath gives it (Windows'
# form, through cygpath, with its trailing separator).
#
# Then the console: the executable is a GUI program (its PE header's subsystem
# is 2, read here with od, since Git Bash has no objdump; skipped for a Linux
# executable), so that Explorer opens no console window beside it
# (docs/DECISIONS.md, "No console window on Windows"); and what it prints
# still reaches a parent that gave it a pipe, through which --version and
# --keys are read as they are through a file above. The log of a run with no
# console at all is run_windows_no_console.ps1's.
#
# No run uses the per-user directory (docs/DECISIONS.md, the settings task):
# each passes --data-dir, both file paths headless, or is portable, so the
# machine's own %APPDATA% is never written. The script runs on Linux as well,
# against the Linux executable (no cygpath there: paths are as given).
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

# run_plain EXECUTABLE DIRECTORY ARGUMENT...: EXECUTABLE started in DIRECTORY
# with the arguments only (--keys, --version); as run otherwise.
run_plain() {
    local executable=$1 directory=$2
    shift 2
    (cd "$directory" && timeout $LIMIT_SECONDS "$executable" "$@") > "$scratch/stdout" 2> "$scratch/stderr"
    code=$?
    for stream in stdout stderr; do
        echo "-- $stream"
        cat "$scratch/$stream"
    done
}

# has_line TEXT: standard output has the line TEXT exactly (a "\r" before the
# newline aside).
has_line() {
    tr -d '\r' < "$scratch/stdout" | grep -qxF -- "$1"
}

# shell_path PATH: a path the program printed, as this shell opens it (Git
# Bash: cygpath -u of Windows' form; elsewhere as it is).
shell_path() {
    if command -v cygpath > /dev/null 2>&1; then
        cygpath -u "$1"
    else
        printf '%s' "$1"
    fi
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

echo "== in an empty directory, with --data-dir data"
work="$scratch/work"
mkdir "$work" || fail "cannot make $work"
run "$work" --data-dir data
check_run "$work/data/pokeemerald.rtc"
[ -e "$work/data/pokeemerald.sav.lock" ] || fail "the data directory has no pokeemerald.sav.lock"
[ "$(cd "$work" && ls -A)" = data ] || fail "the working directory holds more than the data directory: $(cd "$work" && ls -A)"

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

# "deeta" (data) in Katakana.
data_name=$'\xe3\x83\x87\xe3\x83\xbc\xe3\x82\xbf'
echo "== in the non-ASCII directory, with a non-ASCII --data-dir"
run "$unicode_dir" --data-dir "$data_name"
check_run "$unicode_dir/$data_name/pokeemerald.rtc"
[ -e "$unicode_dir/$data_name/pokeemerald.sav.lock" ] || fail "the non-ASCII data directory has no pokeemerald.sav.lock"

echo "== --keys with --data-dir keys, no settings file"
run_plain "$exe" "$work" --keys --data-dir keys
[ $code -eq 0 ] || fail "--keys: exit status $code"
[ ! -s "$scratch/stderr" ] || fail "--keys: standard error is not empty"
has_line "Data directory (--data-dir): keys" || fail "--keys does not name the data directory"
has_line "Save: keys/pokeemerald.sav" || fail "--keys does not name the save in the data directory"
has_line "Clock: keys/pokeemerald.rtc" || fail "--keys does not name the clock's file in the data directory"
has_line "Settings: keys/settings.ini (not there: the defaults)" || fail "--keys does not say the settings file is not there"
has_line "  A       Z" || fail "--keys does not show A on Z, the default"
keys_version=$(head -n 1 "$scratch/stdout" | tr -d '\r')
[[ $keys_version =~ ^pokeemerald-native\ [A-Za-z0-9._+-]+$ ]] || fail "--keys does not start with the version"
[ "$(sed -n 2p "$scratch/stdout" | tr -d '\r')" = "Data directory (--data-dir): keys" ] \
    || fail "--keys does not name the data directory right after the version"

echo "== --keys with a settings file in the data directory"
printf 'a = q\r\nnot a setting\r\n' > "$work/keys/settings.ini" || fail "cannot write keys/settings.ini"
run_plain "$exe" "$work" --keys --data-dir keys
[ $code -eq 0 ] || fail "--keys with settings: exit status $code"
has_line "  A       Q" || fail "--keys does not show A on Q, the settings file's"
has_line "Settings: keys/settings.ini" || fail "--keys does not name the settings file it read"
grep -qF "settings: keys/settings.ini:2: 'not a setting' is not a 'setting = value' line" "$scratch/stderr" \
    || fail "the settings file's bad line 2 is not reported"

echo "== --version"
run_plain "$exe" "$work" --version
[ $code -eq 0 ] || fail "--version: exit status $code"
[ "$(tr -d '\r' < "$scratch/stdout")" = "${keys_version#pokeemerald-native }" ] \
    || fail "--version does not print the version alone, the one --keys printed"

echo "== the PE subsystem is Windows GUI"
if [ "$(head -c 2 "$exe")" = MZ ]; then
    # The PE header's offset is the 32-bit number at 0x3C; the subsystem is the 16-bit number 68 bytes into the
    # optional header, which follows the signature (4 bytes) and the file header (20).
    pe_offset=$(od -An -tu4 -j60 -N4 "$exe" | tr -d ' ')
    subsystem=$(od -An -tu2 -j$((pe_offset + 24 + 68)) -N2 "$exe" | tr -d ' ')
    [ "$subsystem" = 2 ] || fail "the PE subsystem is $subsystem, not 2 (Windows GUI): Explorer would open a console window"
else
    echo "(not a PE file: skipped)"
fi

echo "== --version and --keys through a pipe"
piped=$(cd "$work" && timeout $LIMIT_SECONDS "$exe" --version | cat)
[ "$(printf '%s' "$piped" | tr -d '\r')" = "${keys_version#pokeemerald-native }" ] \
    || fail "--version through a pipe does not print the version"
piped=$(cd "$work" && timeout $LIMIT_SECONDS "$exe" --keys --data-dir keys | cat)
printf '%s\n' "$piped" | tr -d '\r' | grep -qxF "  A       Q" || fail "--keys through a pipe does not show A on Q"
piped=$(cd "$work" && timeout $LIMIT_SECONDS "$exe" --bogus-option 2>&1 | cat)
printf '%s\n' "$piped" | grep -qF "options: bad argument '--bogus-option'" \
    || fail "a bad argument's report does not reach a pipe on standard error"

echo "== portable: a copy of the executable beside a settings.ini"
portable="$scratch/portable"
mkdir "$portable" || fail "cannot make $portable"
cp -- "$exe" "$portable/" || fail "cannot copy the executable"
if [ -f "$(dirname "$exe")/SDL2.dll" ]; then
    cp -- "$(dirname "$exe")/SDL2.dll" "$portable/" || fail "cannot copy SDL2.dll"
fi
printf '# portable\n' > "$portable/settings.ini" || fail "cannot write the portable settings.ini"
marker="marker.$$"
: > "$portable/$marker" || fail "cannot write the marker file"
copy="$portable/$(basename "$exe")"
run_plain "$copy" "$work" --keys
[ $code -eq 0 ] || fail "portable --keys: exit status $code"
# SDL_GetBasePath's form of the directory (on Windows perhaps with short names), so it is checked by
# what is in it, the marker, not by its spelling.
printed=$(tr -d '\r' < "$scratch/stdout" | sed -n 's/^Data directory (portable): //p')
[ -n "$printed" ] || fail "portable --keys names no portable data directory"
case $printed in
*/ | *\\) ;;
*) fail "the portable data directory $printed does not end in a separator" ;;
esac
[ -f "$(shell_path "$printed")/$marker" ] || fail "the portable data directory $printed is not the copy's directory"
has_line "Save: ${printed}pokeemerald.sav" || fail "portable --keys does not put the save beside the executable"
has_line "Clock: ${printed}pokeemerald.rtc" || fail "portable --keys does not put the clock's file beside the executable"
has_line "Settings: ${printed}settings.ini" || fail "portable --keys does not read the settings beside the executable"
rm -f -- "$copy" # its commit charge ends with its file (docs/DECISIONS.md, the Windows unit tests' copies)

if [ $failed -ne 0 ]; then
    exit 1
fi
echo "PASS: the Windows executable started, ran $FRAMES frames and wrote its clock's offset, in an ASCII and in a non-ASCII directory, and read its save through a non-ASCII path; --keys named the files in a data directory given and in portable mode"
