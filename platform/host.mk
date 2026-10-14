# platform/host.mk: the operating system the native executable runs on, and
# what follows from it. Included by platform/Makefile, platform/m4a/Makefile
# and Makefile.native, so that the platform layer and the game agree.
#
# HOST_OS is the system building it, named as `uname -s` does (it defaults to
# that); only Linux builds so far. TARGET_OS (target.mk, included below) is the
# system the executable runs on: HOST_OS, or `windows` for the cross build.
# Each target OS has one source for the host layer beside the one every POSIX
# system shares (platform/host_*.h are the interface; docs/ARCHITECTURE.md,
# "The host layer"), so a port adds its file and its value here. Named HOST_OS,
# not OS, because Windows sets OS in every environment and make would take it.
HOST_OS ?= $(shell uname -s)

ifneq ($(HOST_OS),Linux)
$(error HOST_OS must be Linux (the only system that builds so far), not '$(HOST_OS)')
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
# or a stale map behind.
write_symbol_map = rm -f $(2) $(2).nm $(2).tmp \
	&& $(NM) -p --defined-only --format=sysv $(1) > $(2).nm \
	&& awk -F'|' '{ for (i = 1; i <= 5; i++) gsub(/ /, "", $$i) } $(SYMBOL_MAP_SELECT) { print $$2, $$1 }' $(2).nm > $(2).tmp \
	&& LC_ALL=C sort -s -k1,1 -o $(2).tmp $(2).tmp \
	&& test -s $(2).tmp \
	&& rm -f $(2).nm && mv $(2).tmp $(2) \
	|| { rm -f $(2) $(2).nm $(2).tmp; echo "write_symbol_map: no function map for $(1)" >&2; false; }

# The awk condition that picks the functions out of nm's System V columns
# (name, value, class, type, size). ELF types them FUNC and sizes them, and a
# function with no size is an assembler label; a PE symbol table has types
# ("Function") but no sizes, and an address of zero is a symbol the link
# script gave the value zero (gba_game.pe.ld), not a function.
# HOST_TEST_DIRS names the directories under test/ with the tests only this
# target OS can run, widest family first (posix: fork, signals, pipes, file
# permissions; linux: what only Linux has, /proc and glibc's
# pthread_getattr_np), and HOST_TEST_SUPPORT_DIR the one of them that holds
# the support (test/test_support.h's implementation, support.c). macOS will
# name posix and darwin; Windows its own, once written. A test directory is
# built for its OS and no other: see README.md, "Tests that need an operating
# system".
ifeq ($(TARGET_OS),Linux)

HOST_SRCS := host_posix.c host_linux.c
HOST_TEST_DIRS := posix linux
HOST_TEST_SUPPORT_DIR := posix
SYMBOL_MAP_SELECT = $$4 == "FUNC" && $$5 != "" && $$1 != ""

else ifeq ($(TARGET_OS),windows)

HOST_SRCS := host_windows.c
HOST_TEST_DIRS :=
HOST_TEST_SUPPORT_DIR :=
SYMBOL_MAP_SELECT = $$4 == "Function" && $$1 != "" && $$2 !~ /^0+$$/

endif
