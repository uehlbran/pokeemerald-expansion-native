#!/usr/bin/env bash
# tests/smoke/run.sh: gate (d), the opening replay, against a built binary.
#
#   tests/smoke/run.sh BINARY OUTDIR            run and compare; exit 0 if identical
#   tests/smoke/run.sh BINARY OUTDIR --update   run, then rewrite tests/smoke/expected/
#
# Builds nothing. The work is in lib.sh, shared with run_debug_menu.sh; README.md
# says what is compared. BINARY is build/native32/pokeemerald-native or the SANITIZE=1
# one; run that with ASAN_OPTIONS=halt_on_error=0.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMOKE_NAME=run.sh
SMOKE_SCRIPT="$here/opening.replay"
SMOKE_EXPECTED="$here/expected"
SMOKE_USAGE="run.sh BINARY OUTDIR [--update]"
# What was saved (25_saved, before the restart) is what Continue loaded (27_continued).
SMOKE_ROUND_TRIP="25_saved:27_continued:player_name,map,party0_species,party_count"
. "$here/lib.sh"
smoke_main "$@"
