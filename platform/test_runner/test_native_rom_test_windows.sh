#!/bin/bash
# Tests native-rom-test's Windows path (run_on_windows) on Linux: uname is
# shimmed to answer as MSYS2's does, and the runner is a fake (a bash script,
# which native-rom-test copies to a name ending in .exe as it copies the
# hydra's). A runner that exits 75 once, with a crash report, is started again
# with the state from the report on the same standard output and the run ends
# with the second start's status; one that crashes on every start is not
# started again past the bound; exit 75 without a report, and any other
# status, end the run at once; a TERM ends the runner before the copy is
# removed; no copy or report file is left behind.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
nrt=$here/native-rom-test
CHECKED=native-rom-test
. "$here/script_checks.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir "$tmp/bin"
printf '#!/bin/sh\necho MSYS_NT-10.0-20348\n' > "$tmp/bin/uname"
chmod +x "$tmp/bin/uname"
# 512 hex digits, as crash_reboot writes them.
state=$(printf '0123456789abcdef%.0s' $(seq 32))
report="crash_reboot: exception 0xC0000005 at 0x40001000, fault address 0x0: ending with exit code 75 for the parent to start the program again with --persistent-state $state"
bound=$(sed -n 's/^max_crash_restarts=\([0-9]*\)$/\1/p' "$nrt")

# fake NAME BODY: a runner that counts its starts in $0.starts and records
# its arguments in $0.args, then runs BODY.
fake() {
    printf '#!/bin/bash\necho "$*" >> "$0.args"\necho start >> "$0.starts"\n%s\n' "$2" > "$tmp/$1"
    chmod +x "$tmp/$1"
}

# run NAME: native-rom-test on the fake as the hydra runs it; its status in
# $status, its output in $tmp/NAME.out and .err. A run that does not end
# within RUN_SECONDS (a lost bound) is ended with status 124.
RUN_SECONDS=60
run() {
    status=0
    PATH="$tmp/bin:$PATH" timeout "$RUN_SECONDS" "$nrt" -l15 -ClogLevel.gba.dma=16 -Rr0 "$tmp/$1" \
        > "$tmp/$1.out" 2> "$tmp/$1.err" || status=$?
}

starts() {
    wc -l < "$tmp/$1.exe.starts"
}

left_nothing() {
    [ ! -e "$tmp/$1.exe" ] && [ ! -e "$tmp/$1.stderr" ]
}

fake crash_once "if [ ! -e \"\$0.crashed\" ]; then touch \"\$0.crashed\"; echo 'GBA Debug: :Nfirst'; printf '%s\\r\\n' '$report' >&2; exit 75; fi
echo 'GBA Debug: :Nsecond'"
run crash_once
check "a crash once: the second start's status" [ "$status" -eq 0 ]
check "a crash once: started twice" [ "$(starts crash_once)" -eq 2 ]
check "a crash once: the first start has the runner's options and no state" \
    [ "$(sed -n 1p "$tmp/crash_once.exe.args")" = "--headless --silent --unthrottled --test-runner" ]
check "a crash once: the second start has the state from the report" \
    [ "$(sed -n 2p "$tmp/crash_once.exe.args")" = "--headless --silent --unthrottled --test-runner --persistent-state $state" ]
check "a crash once: both starts on the same standard output" \
    [ "$(tr '\n' ' ' < "$tmp/crash_once.out")" = "GBA Debug: :Nfirst GBA Debug: :Nsecond " ]
check "a crash once: the report passed on to standard error" grep -qF "$report" "$tmp/crash_once.err"
check "a crash once: no copy or report left" left_nothing crash_once

fake always_crashes "printf '%s\\n' '$report' >&2; exit 75"
run always_crashes
check "crashing on every start: exit 75" [ "$status" -eq 75 ]
check "crashing on every start: started once and $bound times again" [ "$(starts always_crashes)" -eq $((bound + 1)) ]
check "crashing on every start: says why it stopped" \
    grep -qx "native-rom-test: the runner crashed $((bound + 1)) times; not started again after $bound restarts" "$tmp/always_crashes.err"
check "crashing on every start: no copy or report left" left_nothing always_crashes

fake no_report 'exit 75'
run no_report
check "75 without a report: exit 75" [ "$status" -eq 75 ]
check "75 without a report: started once" [ "$(starts no_report)" -eq 1 ]
check "75 without a report: says so" grep -q "without a crash report on standard error" "$tmp/no_report.err"

fake fails 'exit 3'
run fails
check "another status: passed on" [ "$status" -eq 3 ]
check "another status: started once" [ "$(starts fails)" -eq 1 ]

fake sleeps 'echo $$ > "$0.pid"; exec sleep 60'
PATH="$tmp/bin:$PATH" "$nrt" "$tmp/sleeps" > /dev/null 2>&1 &
script=$!
for _ in $(seq 100); do
    [ -s "$tmp/sleeps.exe.pid" ] && break
    sleep 0.05
done
kill -TERM "$script"
# The fake sleeps for a minute: ending within a few seconds means the runner
# was ended, not waited for.
for _ in $(seq 100); do
    kill -0 "$script" 2>/dev/null || break
    sleep 0.05
done
prompt=true
if kill -0 "$script" 2>/dev/null; then
    prompt=false
    kill -KILL "$script"
fi
status=0
wait "$script" || status=$?
runner=$(cat "$tmp/sleeps.exe.pid")
check "TERM: the script ends at once" $prompt
check "TERM: with status 143" [ "$status" -eq 143 ]
check "TERM: the runner is ended" bash -c "! kill -0 $runner 2>/dev/null"
kill -KILL "$runner" 2>/dev/null || true
check "TERM: no copy or report left" left_nothing sleeps

finish
