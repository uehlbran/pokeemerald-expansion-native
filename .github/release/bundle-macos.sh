#!/usr/bin/env bash
# Copy the dynamic libraries a macOS executable needs into the folder it is
# packaged in, and point it at them with @executable_path, so that the
# archive runs on a Mac with no Homebrew (the release workflow's macOS job).
#
#   .github/release/bundle-macos.sh EXECUTABLE DIRECTORY
#
# The libraries are the ones outside /usr/lib and /System that EXECUTABLE
# names (Homebrew's sdl2-compat, libSDL2-2.0.0.dylib, and whatever that names
# in turn), plus Homebrew's SDL3: sdl2-compat does not link it but opens it
# with dlopen at start-up, from "@loader_path/libSDL3.dylib" first
# (libsdl-org/sdl2-compat, src/sdl2_compat.c, LoadSDL3Library), so it is
# bundled under that name. Each library's licence is copied from its
# Homebrew keg as DIRECTORY/<library>-LICENSE.txt. Fails if a library has no
# licence there, or if anything still names a path outside the system's.
# Written for macOS's bash 3.2 and BSD tools; not runnable elsewhere.
set -euo pipefail

[ $# -eq 2 ] || { echo "usage: bundle-macos.sh EXECUTABLE DIRECTORY" >&2; exit 2; }
exe=$1
dir=$2
sdl3_name=libSDL3.dylib
nl=$'\n'

fail() {
    echo "bundle-macos: $*" >&2
    exit 1
}

# What FILE loads from outside the system, one path a line. A dylib's own
# install name is the first line otool -L prints for it; -D names it.
outside_deps() {
    local id
    id=$(otool -D "$1" | sed -n 2p)
    otool -L "$1" | sed 1d | awk '{ print $1 }' | grep -v -e '^/usr/lib/' -e '^/System/' | grep -vxF -e "$id" || true
}

# The keg's licence: the first LICENSE* or COPYING* file in the Homebrew
# keg the library really lives in (Cellar/NAME/VERSION).
licence_of() {
    local real keg found
    real=$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$1")
    case "$real" in
        */Cellar/*/*/*) keg=$(echo "$real" | sed -E 's|(.*/Cellar/[^/]+/[^/]+)/.*|\1|') ;;
        *) fail "$1 is not in a Homebrew keg ($real)" ;;
    esac
    found=$(find "$keg" \( -iname 'LICENSE*' -o -iname 'COPYING*' \) -type f | sort | head -n 1)
    [ -n "$found" ] || fail "no LICENSE or COPYING file in $keg"
    echo "$found"
}

mkdir -p "$dir"
sdl3_dir=$(brew --prefix sdl3)
sdl3="$sdl3_dir/lib/libSDL3.0.dylib"
[ -e "$sdl3" ] || fail "Homebrew's SDL3 is not at $sdl3"

# Breadth first from the executable's libraries and SDL3; "source|name" a line.
queue="$(outside_deps "$exe" | while read -r p; do echo "$p|$(basename "$p")"; done)$nl$sdl3|$sdl3_name"
seen=
while [ -n "$queue" ]; do
    item=${queue%%"$nl"*}
    if [ "$item" = "$queue" ]; then queue=; else queue=${queue#*"$nl"}; fi
    [ -n "$item" ] || continue
    src=${item%%|*}
    name=${item#*|}
    case "$nl$seen" in *"$nl$src|"*) continue ;; esac
    seen="$seen$src|$name$nl"
    [ ! -e "$dir/$name" ] || fail "two libraries are named $name"
    cp -L "$src" "$dir/$name"
    chmod u+w "$dir/$name"
    cp "$(licence_of "$src")" "$dir/${name%.dylib}-LICENSE.txt"
    queue="$queue$nl$(outside_deps "$dir/$name" | while read -r p; do echo "$p|$(basename "$p")"; done)"
done

# Rewrite: every library's own name, and every reference to a bundled one
# (the references were read before any rewriting, from the copies).
rewrite() {
    local file=$1 dep
    for dep in $(outside_deps "$file"); do
        install_name_tool -change "$dep" "@executable_path/$(basename "$dep")" "$file"
    done
}
rewrite "$exe"
for f in "$dir"/*.dylib; do
    install_name_tool -id "@executable_path/$(basename "$f")" "$f"
    rewrite "$f"
done

# install_name_tool leaves a signature it has broken, which the system treats
# worse than none: an ad-hoc one (no identity, no notarization) is valid.
for f in "$dir"/*.dylib "$exe"; do
    codesign --force --sign - "$f"
done

# Nothing may still name a place outside the system and the bundle.
status=0
for f in "$dir"/*.dylib "$exe"; do
    echo "== $f"
    otool -L "$f"
    if otool -L "$f" | sed 1d | awk '{ print $1 }' | grep -v -e '^/usr/lib/' -e '^/System/' -e '^@executable_path/'; then
        echo "bundle-macos: $f names a path outside the system and the bundle" >&2
        status=1
    fi
done
exit $status
