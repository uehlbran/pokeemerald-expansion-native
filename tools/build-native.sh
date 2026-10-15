#!/usr/bin/env bash
# One-command build of the native game, 64-bit, for Linux or Windows.
#
#   tools/build-native.sh --os linux|windows [--jobs N]
#
# Runs the steps docs/BUILDING.md lists, from a fresh checkout: it checks
# the prerequisites and names what is missing, then makes the game
# (Makefile.native builds the asset tools, converts the graphics, builds the
# data and its checks, compiles and links; for Windows it also fetches
# SDL2 for MinGW, checked against the sha256 recorded in Makefile.native)
# and prints where the executable is. It installs nothing.
#
# macOS is not supported yet. 32-bit and sanitized builds are the Makefile's
# own (docs/BUILDING.md), not this script's.
set -euo pipefail

usage() {
    echo "usage: tools/build-native.sh --os linux|windows [--jobs N]" >&2
    exit 2
}

target_os=
jobs=$(nproc)
while [ $# -gt 0 ]; do
    case "$1" in
        --os)
            [ $# -ge 2 ] || usage
            target_os=$2
            shift 2
            ;;
        --jobs)
            [ $# -ge 2 ] || usage
            jobs=$2
            shift 2
            ;;
        *) usage ;;
    esac
done

case "$target_os" in
    linux)
        make_args=(native BITS=64)
        exe=build/native64/pokeemerald-native
        ;;
    windows)
        make_args=(native BITS=64 TARGET_OS=windows)
        exe=build/native64-windows/pokeemerald-native.exe
        ;;
    *) usage ;;
esac
case "$jobs" in
    '' | *[!0-9]* | 0) usage ;;
esac

cd "$(dirname "$0")/.."
[ -f Makefile.native ] || { echo "build-native: Makefile.native not found: run it from the repository" >&2; exit 1; }

# Commands needed (the package that provides each, Debian and Ubuntu names).
missing=()
need_command() {
    command -v "$1" > /dev/null 2>&1 || missing+=("$1 (package $2)")
}
need_command gcc build-essential
need_command make build-essential
need_command git git
need_command python3 python3
need_command pkg-config pkg-config
need_command tar tar
need_command sha256sum coreutils
if command -v pkg-config > /dev/null 2>&1; then
    pkg-config --exists libpng || missing+=("the libpng headers (package libpng-dev)")
fi
# The 64-bit data's layout checks compile a probe at both widths.
if command -v gcc > /dev/null 2>&1; then
    printf 'int main(void) { return 0; }\n' | gcc -m32 -x c -o /dev/null - 2> /dev/null \
        || missing+=("a 32-bit C library for gcc -m32 (packages gcc-multilib libc6-dev-i386)")
fi
if [ "$target_os" = linux ]; then
    if command -v pkg-config > /dev/null 2>&1; then
        pkg-config --exists sdl2 || missing+=("the SDL2 headers (package libsdl2-dev)")
    fi
else
    need_command curl curl # fetches SDL2 for MinGW
    need_command x86_64-w64-mingw32-gcc gcc-mingw-w64-x86-64
    need_command x86_64-w64-mingw32-windres binutils-mingw-w64-x86-64
    need_command x86_64-w64-mingw32-as binutils-mingw-w64-x86-64
    need_command clang clang
fi
if [ ${#missing[@]} -gt 0 ]; then
    echo "build-native: missing prerequisites (docs/BUILDING.md has the apt-get lines):" >&2
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

echo "build-native: make -f Makefile.native -j$jobs ${make_args[*]}"
make -f Makefile.native -j"$jobs" "${make_args[@]}"

[ -f "$exe" ] || { echo "build-native: the make succeeded but $exe is not there" >&2; exit 1; }
echo "build-native: done: $PWD/$exe"
