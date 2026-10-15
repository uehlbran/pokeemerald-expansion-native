#!/bin/sh
# Starts the game with the SDL2 library beside it (the package's own copy)
# rather than the system's, from wherever the folder was copied to and
# however it was launched (a terminal, a file manager, Steam's "Add a
# Non-Steam Game"). The working directory is left as it was, so relative
# arguments (--replay, --save, --data-dir) mean what the caller meant; the
# game finds a portable settings.ini beside its executable by itself.
here=$(dirname "$(readlink -f "$0")") || exit 1
LD_LIBRARY_PATH="$here${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
exec "$here/pokeemerald-native" "$@"
