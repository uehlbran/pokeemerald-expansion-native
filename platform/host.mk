# platform/host.mk: the operating system the native executable runs on, and
# what follows from it. Included by platform/Makefile, platform/m4a/Makefile
# and Makefile.native, so that the platform layer and the game agree.
#
# Three terms, kept apart: HOST_OS is the machine building (named as `uname -s`
# does, and defaulting to that: Linux or Darwin, macOS), TARGET_OS (target.mk,
# included below) is the OS the executable runs on (HOST_OS, or `windows` for
# the cross build from Linux), and the host layer is the executable's view of
# that OS. The compiler and assembler for the executable are therefore
# TARGET_CC and TARGET_AS.
# Each target OS has its sources for the host layer (platform/host_*.h are the
# interface; docs/ARCHITECTURE.md, "The host layer"): Linux and macOS the one
# every POSIX system shares and their own, Windows its own; all three
# host_linked.c, which reads the region symbols the GNU ld scripts define on
# Linux and Windows and the link flags name on macOS (target.mk). A port adds
# its files and its value here. Named HOST_OS, not OS, because Windows sets OS
# in every environment and make would take it.
HOST_OS ?= $(shell uname -s)

ifeq (,$(filter Linux Darwin,$(HOST_OS)))
$(error HOST_OS must be Linux or Darwin (macOS), the systems that build, not '$(HOST_OS)')
endif

include $(dir $(lastword $(MAKEFILE_LIST)))target.mk

NM ?= nm

# $(call write_symbol_map,EXECUTABLE,MAP): the function map symbols.c reads
# to name the game's callbacks (symbols.h has the format), written by the
# executable's own link recipe, so that the two never part. nm's System V
# format names each symbol's type, so the map holds what the symbol table
# calls a function (SYMBOL_MAP_SELECT, below, says how each format names it);
# -p keeps the table's order, which the stable
# sort by address keeps among aliases (the first is the name the replays have
# always shown). The addresses are nm's fixed-width lower-case hexadecimal,
# so a text sort is a numeric one. No pipe, so that every step's failure is
# the recipe's (sh has no pipefail): any failure, or a map with no function,
# removes the old map and the temporaries and fails, never leaving an empty
# or a stale map behind. SYMBOL_MAP_NAME is the awk expression of a symbol's
# C name: the table's own, but for Mach-O's leading underscore.
SYMBOL_MAP_NAME = $$1
write_symbol_map = rm -f $(2) $(2).nm $(2).tmp \
	&& $(NM) -p --defined-only --format=sysv $(1) > $(2).nm \
	&& awk -F'|' '{ for (i = 1; i <= 5; i++) gsub(/ /, "", $$i) } $(SYMBOL_MAP_SELECT) { print $$2, $(SYMBOL_MAP_NAME) }' $(2).nm > $(2).tmp \
	&& LC_ALL=C sort -s -k1,1 -o $(2).tmp $(2).tmp \
	&& test -s $(2).tmp \
	&& rm -f $(2).nm && mv $(2).tmp $(2) \
	|| { rm -f $(2) $(2).nm $(2).tmp; echo "write_symbol_map: no function map for $(1)" >&2; false; }

ifeq ($(TARGET_OS),Linux)

HOST_SRCS := host_posix.c host_linux.c host_linked.c
# HOST_TEST_DIRS: the directories under test/ with the tests only this target OS
# can run, widest family first (posix: fork, signals, pipes, file permissions;
# linux: what only Linux has, /proc and glibc's pthread_getattr_np). A test
# directory is built for its OS and no other (README.md, "Tests that need an
# operating system"); macOS names posix and darwin, Windows names windows.
# HOST_TEST_SUPPORT_DIR is the one that holds test_support.h's implementation.
HOST_TEST_DIRS := posix linux
HOST_TEST_SUPPORT_DIR := posix
# SYMBOL_MAP_SELECT: the awk condition that picks the functions out of nm's
# System V columns (name, value, class, type, size). ELF types them FUNC and
# sizes them; a function with no size is an assembler label.
SYMBOL_MAP_SELECT = $$4 == "FUNC" && $$5 != "" && $$1 != ""

else ifeq ($(TARGET_OS),Darwin)

HOST_SRCS := host_posix.c host_macos.c host_macos_stack.c host_linked.c
# posix as on Linux, and darwin: what only macOS has (_NSGetExecutablePath, the
# Mach-O image's load commands, F_FULLFSYNC, its own system calls' numbers).
HOST_TEST_DIRS := posix darwin
HOST_TEST_SUPPORT_DIR := posix
# Mach-O symbols have neither types nor sizes: nm's class says which are in a
# code section (T, t), and a C name carries a leading underscore.
SYMBOL_MAP_SELECT = ($$3 == "T" || $$3 == "t") && $$1 != ""
SYMBOL_MAP_NAME = (substr($$1, 1, 1) == "_" ? substr($$1, 2) : $$1)

else ifeq ($(TARGET_OS),windows)

HOST_SRCS := host_windows.c host_windows_console.c host_windows_fault.c host_windows_stack.c host_windows_thread.c host_linked.c
HOST_TEST_DIRS := windows
HOST_TEST_SUPPORT_DIR := windows
# A PE symbol table has types ("Function") but no sizes, and an address of zero
# is a symbol the link script gave the value zero (gba_game.pe.ld), not a
# function.
SYMBOL_MAP_SELECT = $$4 == "Function" && $$1 != "" && $$2 !~ /^0+$$/

endif
