#!/bin/sh
# Starts the game from the folder this script is in, wherever it was copied
# to and however it was launched (a terminal, a file manager, Steam's "Add a
# Non-Steam Game"): the settings.ini beside it, if any, is the game's
# portable-mode file. Arguments go to the game.
here=$(dirname "$(readlink -f "$0")") || exit 1
cd "$here" || exit 1
exec "$here/pokeemerald-native" "$@"
