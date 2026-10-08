# platform/Makefile: builds the platform layer and its unit tests standalone,
# without expansion's build. Run from the repository root as
#
#   make -C platform test          build and run every unit test (BITS=32)
#   make -C platform test BITS=64  the same for a 64-bit build
#   make -C platform lib           build build/<bits>/libplatform.a
#   make -C platform lib NOSAN=1   the same without sanitizers, in
#                                  build/<bits>-nosan/ (Makefile.native SANITIZE=0)
#   make -C platform clean         remove build/
#
# Layout: every platform/*.c is a module and goes into libplatform.a, so a new
# module needs no edit here. The m4a engine (platform/m4a/m4a_*.c, the C
# translation of src/m4a_1.s) goes into libplatform.a too; it calls into
# expansion's src/m4a.c and src/m4a_tables.c, which the game link provides. Every platform/test/test_<name>.c is a test
# binary, build/<bits>/test_<name>, linked with:
#   - platform/<name>.c, if it exists (the module under test);
#   - the files named on a "// deps:" line near the top of the test, paths
#     relative to platform/, for example
#         // deps: gba_memmap.c interrupt.c
#     (a leading "#" after the slashes, "// # deps:", is accepted too).
# Tests include "ptest.h" (platform/test/ptest.h) and their module's header.

BITS ?= 32
ifeq ($(filter $(BITS),32 64),)
$(error BITS must be 32 or 64, not '$(BITS)')
endif

# NOSAN=1 builds libplatform.a without the sanitizers, for the native game
# executable when Makefile.native's SANITIZE=0 (expansion's objects are then
# not instrumented either, and an ASan runtime would have to come first in the
# link). It applies to `lib` only: the unit tests always run sanitized
# (docs/C-RULES.md), so NOSAN=1 with any other goal is an error. Its objects
# live in their own directory so the two builds never mix.
NOSAN ?= 0
ifeq ($(NOSAN),1)
ifneq ($(filter-out lib clean,$(or $(MAKECMDGOALS),all)),)
$(error NOSAN=1 is for the lib goal only; the unit tests always run under the sanitizers)
endif
BUILD_DIR := build/$(BITS)-nosan
else ifeq ($(NOSAN),0)
BUILD_DIR := build/$(BITS)
else
$(error NOSAN must be 0 or 1, not '$(NOSAN)')
endif
OBJ_DIR   := $(BUILD_DIR)/obj

# gnu17 is the C dialect expansion's modern toolchain compiles with
# (docs/C-RULES.md). Warnings are errors, for our files only.
WARNINGS := -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion -Werror

# -O1, not -O0 or -O2: GCC's flow-based warnings (-Wmaybe-uninitialized,
# -Warray-bounds, -Wstringop-overflow, -Wnonnull after inlining) only run with
# optimisation, so -O0 would hide bugs; -O1 keeps code close enough to the
# source that sanitizer reports and debugger backtraces stay accurate, which
# -O2's inlining and reordering would blur, and keeps compile times short.
OPTIMISATION := -O1 -g

# AddressSanitizer (with LeakSanitizer, on by default on Linux) and
# UndefinedBehaviorSanitizer on every object, tests and libplatform.a alike,
# unconditionally (docs/C-RULES.md; "override" ignores command-line attempts
# to change it); any report aborts the binary.
override SANITIZERS := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
ifeq ($(NOSAN),1)
override SANITIZERS :=
endif

# Expansion's headers (../include) are system headers to us, so their warnings
# stay out of our -Werror build: we do not rewrite or re-lint expansion's code
# (docs/ARCHITECTURE.md). -idirafter rather than -isystem: both mark the
# directory as a system directory, but -isystem searches it before the C
# library's headers, and expansion's include/ has strings.h, malloc.h, link.h
# and menu.h, so with -isystem glibc's <string.h> picks up expansion's
# strings.h and nothing compiles. -idirafter searches it after them; expansion's
# headers are reached by their own paths, e.g. <gba/defines.h>. Our own
# headers come from -I. The repository root is on the quote path (-iquote ..)
# because expansion's include/gba/macro.h includes "platform/dma_hooks.h"
# under PLATFORM_NATIVE, as Makefile.native compiles it; only quote includes
# search it, and it is not a system directory, so dma_hooks.h keeps our warnings.
INCLUDES := -I. -iquote .. -idirafter ../include

# SDL2, for the modules that talk to the host (input, video, audio) and their
# tests; harmless for the others. The i386 and amd64 dev packages share flags.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
ifeq ($(SDL_LIBS),)
$(error pkg-config cannot find sdl2; install libsdl2-dev (and libsdl2-dev:i386 for BITS=32))
endif

CFLAGS_ALL := -m$(BITS) -std=gnu17 $(WARNINGS) $(OPTIMISATION) $(SANITIZERS) $(INCLUDES) \
              $(SDL_CFLAGS) -MMD -MP

# The GBA memory regions exist because the ELF loader maps them: gba_regions.ld
# adds them as sections at their GBA addresses (see gba_memmap.c), which needs
# a non-PIE executable whose own image starts at 0x40000000, clear of every
# region on both bit widths (docs/research/memory-map-experiment.md).
# Makefile.native must link the game with these same flags.
REGIONS_LD := gba_regions.ld
# gba_game.ld adds what expansion's linker scripts give the game (symbols,
# the test table, the brackets around the game's variables); in a unit test
# its sections are empty, but its symbols exist, as crt0.c needs.
GAME_LD := gba_game.ld
GBA_LDFLAGS := -no-pie -Wl,-Ttext-segment=0x40000000 -Wl,-T,$(REGIONS_LD) -Wl,-T,$(GAME_LD)
LDFLAGS_ALL := -m$(BITS) $(SANITIZERS) $(GBA_LDFLAGS)

# The sanitizer options every test runs under, set here so the environment
# cannot weaken them: leaks detected, no suppressions file, UBSan backtraces.
override SANITIZER_ENV := ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS= UBSAN_OPTIONS=print_stacktrace=1

MODULE_SRCS := $(wildcard *.c) $(wildcard m4a/m4a_*.c)
MODULE_OBJS := $(MODULE_SRCS:%.c=$(OBJ_DIR)/%.o)
TEST_SRCS   := $(wildcard test/test_*.c)
TEST_NAMES  := $(TEST_SRCS:test/%.c=%)
TEST_BINS   := $(TEST_NAMES:%=$(BUILD_DIR)/%)
LIB         := $(BUILD_DIR)/libplatform.a

.PHONY: all lib test clean
all: lib test

lib: $(LIB)

$(LIB): $(MODULE_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_ALL) -c $< -o $@

# The files a test names on its "// deps:" line.
test_deps = $(shell sed -n -E 's@^//[[:space:]]*\#?[[:space:]]*deps:@@p' test/$(1).c)
# The module under test, if the test is named after one, plus its deps.
test_inputs = $(wildcard $(1:test_%=%).c) $(call test_deps,$(1))

define TEST_LINK_RULE
$(foreach dep,$(call test_deps,$(1)),$(if $(wildcard $(dep)),,$(error test/$(1).c lists '$(dep)' on its deps line, but platform/$(dep) does not exist)))
$(BUILD_DIR)/$(1): $(OBJ_DIR)/test/$(1).o $(patsubst %.c,$(OBJ_DIR)/%.o,$(call test_inputs,$(1))) $(REGIONS_LD) $(GAME_LD)
	$$(CC) $$(LDFLAGS_ALL) -o $$@ $$(filter %.o,$$^) $$(SDL_LIBS)
endef
$(foreach name,$(TEST_NAMES),$(eval $(call TEST_LINK_RULE,$(name))))

# Runs every test even after a failure, then fails if any test exited non-zero
# or printed a sanitizer report (a report that did not change the exit status
# still fails the run).
test: $(TEST_BINS)
	@status=0; \
	for bin in $(TEST_BINS); do \
		echo "== $$bin"; \
		$(SANITIZER_ENV) ./$$bin > $$bin.log 2>&1; code=$$?; \
		cat $$bin.log; \
		if [ $$code -ne 0 ]; then \
			echo "FAILED: $$bin exited with status $$code"; status=1; \
		elif grep -qE 'Sanitizer|runtime error:' $$bin.log; then \
			echo "FAILED: $$bin printed a sanitizer report"; status=1; \
		fi; \
	done; \
	if [ $$status -eq 0 ]; then echo "all platform tests passed (BITS=$(BITS))"; fi; \
	exit $$status

clean:
	rm -rf build

-include $(wildcard $(OBJ_DIR)/*.d $(OBJ_DIR)/test/*.d $(OBJ_DIR)/m4a/*.d)
