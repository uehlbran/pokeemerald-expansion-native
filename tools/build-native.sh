#!/usr/bin/env bash
# One-command build of the native game, 64-bit, for Linux, Windows or macOS.
#
#   tools/build-native.sh --os linux|windows|macos [--jobs N]
#
# Runs the steps docs/BUILDING.md lists, from a fresh checkout: it checks
# the prerequisites and names what is missing, then makes the game
# (Makefile.native builds the asset tools, converts the graphics, builds the
# data and its checks, compiles and links; for Windows it also fetches
# SDL2 for MinGW, checked against the sha256 recorded in Makefile.native)
# and prints where the executable is. It installs nothing.
#
# --os macos runs on a Mac, with Homebrew's tools (docs/BUILDING.md, "macOS");
# it has not run on one yet. 32-bit and sanitized builds are the Makefile's
# own (docs/BUILDING.md), not this script's.
set -euo pipefail

usage() {
    echo "usage: tools/build-native.sh --os linux|windows|macos [--jobs N]" >&2
    exit 2
}

target_os=
jobs=
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
    macos)
        make_args=(native BITS=64)
        exe=build/native64/pokeemerald-native
        ;;
    *) usage ;;
esac
if [ -z "$jobs" ]; then
    # macOS has no nproc until coreutils' unprefixed names are on PATH.
    jobs=$(nproc 2> /dev/null || sysctl -n hw.ncpu)
fi
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

# What a Mac needs: the same list as docs/BUILDING.md's brew line, and GNU
# make and coreutils' names first on PATH, which the build and its scripts
# are written for. The executable is x86_64, so an Apple-silicon Mac needs an
# x86_64 Homebrew (/usr/local) and this script run under Rosetta.
need_mac() {
    command -v "$1" > /dev/null 2>&1 || missing+=("$1 ($2)")
}

check_macos_prerequisites() {
    [ "$(uname -s)" = Darwin ] || { echo "build-native: --os macos builds on a Mac only (this is $(uname -s))" >&2; exit 1; }
    case "$(uname -m)" in
        x86_64)
            if [ "$(sysctl -n sysctl.proc_translated 2> /dev/null || echo 0)" = 1 ]; then
                echo "build-native: running under Rosetta (Apple silicon) with an x86_64 Homebrew: this setup is unverified" >&2
            fi
            ;;
        *)
            echo "build-native: this is an arm64 process. The macOS executable is x86_64, which Homebrew's arm64 packages" >&2
            echo "  (/opt/homebrew) cannot be linked into. On Apple silicon install Homebrew for x86_64 under Rosetta" >&2
            echo "  (arch -x86_64 /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\")," >&2
            echo "  install the packages with /usr/local/bin/brew, and run this script as: arch -x86_64 tools/build-native.sh --os macos." >&2
            echo "  That setup is unverified: no Apple-silicon Mac or runner has built the game." >&2
            exit 1
            ;;
    esac
    need_mac brew "Homebrew, https://brew.sh"
    need_mac clang "the Command Line Tools: xcode-select --install"
    need_mac git "the Command Line Tools"
    need_mac python3 "the Command Line Tools"
    need_mac tar "the system"
    need_mac pkg-config "brew install pkg-config"
    need_mac x86_64-elf-as "brew install x86_64-elf-binutils"
    if command -v make > /dev/null 2>&1; then
        case "$(make --version 2> /dev/null | sed -n '1s/[^0-9]*\([0-9]*\).*/\1/p')" in
            [4-9] | [1-9][0-9]*) ;;
            *) missing+=("GNU make 4 or later first on PATH (brew install make; PATH gets \$(brew --prefix)/opt/make/libexec/gnubin)") ;;
        esac
    else
        missing+=("make (brew install make)")
    fi
    # Coreutils' unprefixed names; macOS's own readlink has no -f.
    local tool
    for tool in nproc stdbuf sha256sum md5sum timeout; do
        need_mac "$tool" "brew install coreutils; PATH gets \$(brew --prefix)/opt/coreutils/libexec/gnubin"
    done
    if command -v readlink > /dev/null 2>&1; then
        readlink -f / > /dev/null 2>&1 || missing+=("GNU readlink (brew install coreutils; PATH gets \$(brew --prefix)/opt/coreutils/libexec/gnubin)")
    fi
    if command -v pkg-config > /dev/null 2>&1; then
        pkg-config --exists sdl2 || missing+=("the SDL2 headers (brew install sdl2)")
        pkg-config --exists libpng || missing+=("the libpng headers (brew install libpng)")
    fi
}
if [ "$target_os" = macos ]; then
    check_macos_prerequisites
else
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
fi
case "$target_os" in
    linux)
        if command -v pkg-config > /dev/null 2>&1; then
            pkg-config --exists sdl2 || missing+=("the SDL2 headers (package libsdl2-dev)")
        fi
        ;;
    windows)
        need_command curl curl # fetches SDL2 for MinGW
        need_command x86_64-w64-mingw32-gcc gcc-mingw-w64-x86-64
        need_command x86_64-w64-mingw32-windres binutils-mingw-w64-x86-64
        need_command x86_64-w64-mingw32-as binutils-mingw-w64-x86-64
        need_command clang clang
        ;;
esac
if [ ${#missing[@]} -gt 0 ]; then
    if [ "$target_os" = macos ]; then
        echo "build-native: missing prerequisites (docs/BUILDING.md, \"macOS\", has the brew line and the PATH line):" >&2
    else
        echo "build-native: missing prerequisites (docs/BUILDING.md has the apt-get lines):" >&2
    fi
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

echo "build-native: make -f Makefile.native -j$jobs ${make_args[*]}"
make -f Makefile.native -j"$jobs" "${make_args[@]}"

[ -f "$exe" ] || { echo "build-native: the make succeeded but $exe is not there" >&2; exit 1; }
echo "build-native: done: $PWD/$exe"
