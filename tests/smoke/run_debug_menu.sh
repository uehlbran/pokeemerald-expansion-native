#!/usr/bin/env bash
# tests/smoke/run_debug_menu.sh: gate (e), the debug-menu replay, against a built binary.
#
#   tests/smoke/run_debug_menu.sh BINARY OUTDIR            run and compare; exit 0 if identical
#   tests/smoke/run_debug_menu.sh BINARY OUTDIR --update   run, then rewrite tests/smoke/expected_debug_menu/
#
# Builds nothing. The work is in lib.sh, shared with run.sh; README.md says what is compared.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMOKE_NAME=run_debug_menu.sh
SMOKE_SCRIPT="$here/debug_menu.replay"
SMOKE_EXPECTED="$here/expected_debug_menu"
SMOKE_USAGE="run_debug_menu.sh BINARY OUTDIR [--update]"
# The party, the PC box, the bag, a flag and a var, saved (14_saved) and loaded by Continue (15_continued).
SMOKE_ROUND_TRIP="14_saved:15_continued:player_name,map,party_count,party_species,pc_box0_count"
. "$here/lib.sh"
smoke_main "$@"
