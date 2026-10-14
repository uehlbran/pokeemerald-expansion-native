# platform/Makefile: builds the platform layer and its unit tests standalone,
# without expansion's build. Run from the repository root as
#
#   make -C platform test          build and run every unit test (BITS=32)
#   make -C platform test BITS=64  the same for a 64-bit build
#   make -C platform lib           build build/<bits>/libplatform.a
#   make -C platform lib NOSAN=1   the same without sanitizers, in
#                                  build/<bits>-nosan/ (Makefile.native SANITIZE=0)
#   make -C platform lib TARGET_OS=windows BITS=64    libplatform.a for Windows (MinGW-w64)
#   make -C platform test TARGET_OS=windows BITS=64   cross-build the unit tests into
#                                  build/64-windows/test/*.exe, not run (Windows runs them:
#                                  test_runner/run_windows_tests.sh)
#   make -C platform format        apply clang-format (../.clang-format) to every file below
#   make -C platform format-check  fail, naming the files, if any would change
#   make -C platform lint          clang-tidy (../.clang-tidy) and cppcheck over every .c file;
#                                  any finding fails
#   make -C platform tsan          the unit tests under ThreadSanitizer (64-bit, build/64-tsan)
#   make -C platform gate          the platform gate: format-check, lint and test for both widths, tsan, tools-test, glue-test
#   make -C platform clean         remove build/
#
# Layout: every platform/*.c is a module and goes into libplatform.a, so a new
# module needs no edit here; of the host layer's sources (host_*.c), only
# those of HOST_OS (host.mk, Linux by default) do. The m4a engine
# (platform/m4a/m4a_*.c, the C translation of src/m4a_1.s) goes into
# libplatform.a too; it calls into expansion's src/m4a.c and src/m4a_tables.c,
# which the game link provides.
# Every platform/test/test_<name>.c is a test binary, build/<bits>/test_<name>,
# linked with:
#   - platform/<name>.c, if it exists (the module under test);
#   - the files named on a "// deps:" line near the top of the test, paths
#     relative to platform/, for example
#         // deps: gba_memmap.c interrupt.c
#     (a leading "#" after the slashes, "// # deps:", is accepted too);
#   - the host layer's sources, which every test may reach through a module.
# Tests include "ptest.h" (platform/test/ptest.h) and their module's header.

include game_flags.mk

BITS ?= 32
ifeq ($(filter $(BITS),32 64),)
$(error BITS must be 32 or 64, not '$(BITS)')
endif

include host.mk

# TARGET_OS=windows (target.mk) builds for Windows with MinGW-w64, which has no
# sanitizers, into build/<bits>-windows: libplatform.a for the game, and the
# unit tests, which are cross-built and not run here, since they run where the
# executable does (test_runner/run_windows_tests.sh, on Windows); `lint`,
# clang-tidy for the MinGW-w64 target and cppcheck, over what the Windows build
# compiles; and `tools-test`, tools/patchelf on a Windows executable.
ifeq ($(TARGET_OS),windows)
ifneq ($(filter-out lib test lint lint-tidy lint-cppcheck tools-test clean,$(or $(MAKECMDGOALS),all)),)
$(error TARGET_OS=windows builds the lib, test, lint and tools-test goals only: MinGW-w64 has no sanitizers, and the tests run on Windows)
endif
endif

# NOSAN=1 builds libplatform.a without the sanitizers, for the native game
# executable when Makefile.native's SANITIZE=0 (expansion's objects are then
# not instrumented either, and an ASan runtime would have to come first in the
# link). It applies to `lib` only: the unit tests always run sanitized
# (docs/C-RULES.md), so NOSAN=1 with any other goal is an error. Its objects
# live in their own directory so the two builds never mix.
# TSAN=1 builds and runs the unit tests under ThreadSanitizer instead of
# AddressSanitizer and UndefinedBehaviorSanitizer (the two cannot combine),
# 64-bit only, in build/64-tsan: the platform is single-threaded except for
# the test time limit's watchdog thread (host_thread.h), which every test
# binary starts, and this proves that thread touches nothing but its flag
# (docs/C-RULES.md, "Concurrency"). `make -C platform tsan` is the shortcut.
TSAN ?= 0
ifeq ($(TSAN),1)
ifneq ($(BITS),64)
$(error TSAN=1 is for BITS=64 only)
endif
endif

NOSAN ?= 0
ifeq ($(TARGET_OS),windows)
BUILD_DIR := build/$(BITS)-windows
else ifeq ($(TSAN),1)
BUILD_DIR := build/64-tsan
else ifeq ($(NOSAN),1)
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
ifeq ($(TSAN),1)
override SANITIZERS := -fsanitize=thread -fno-omit-frame-pointer
endif
ifneq ($(filter 1,$(NOSAN))$(filter windows,$(TARGET_OS)),)
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
INCLUDES := -I. -Itest -iquote .. -idirafter ../include

# SDL2, for the modules that talk to the host (input, video, audio) and their
# tests; harmless for the others. The i386 and amd64 dev packages share flags.
# SDL_CFLAGS and SDL_LIBS come from target.mk.
ifeq ($(SDL_LIBS),)
$(error pkg-config cannot find sdl2; install libsdl2-dev (and libsdl2-dev:i386 for BITS=32))
endif
ifeq ($(TARGET_OS),windows)
ifeq ($(wildcard $(SDL2_MINGW_DIR)/include/SDL2/SDL.h),)
$(error SDL2 for MinGW is not in $(SDL2_MINGW_DIR): make -f Makefile.native sdl2-mingw TARGET_OS=windows BITS=64)
endif
endif

# A 64-bit time_t and off_t on the 32-bit build too, in every file: the
# clock's dates run to 2099, past a 32-bit time_t's 2038 (rtc.c and
# host_posix.c assert it). _TIME_BITS=64 needs _FILE_OFFSET_BITS=64.
LARGE_TYPES := -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64

CFLAGS_ALL := -m$(BITS) -std=gnu17 $(WARNINGS) $(OPTIMISATION) $(SANITIZERS) $(INCLUDES) $(LARGE_TYPES) \
              $(SDL_CFLAGS) $(TARGET_CFLAGS) -MMD -MP

# The GBA memory regions exist because the ELF loader maps them: gba_regions.ld
# adds them as sections at their GBA addresses (see gba_memmap.c), which needs
# a non-PIE executable whose own image starts at 0x40000000, clear of every
# region on both bit widths (docs/research/memory-map-experiment.md).
# Makefile.native must link the game with these same flags.
REGIONS_LD := $(REGIONS_LD_NAME)
# gba_game.ld adds what expansion's linker scripts give the game (symbols,
# the test table, the brackets around the game's variables); in a unit test
# its sections are empty, but its symbols exist, as crt0.c needs.
GAME_LD := $(GAME_LD_NAME)
GBA_LDFLAGS := $(GBA_LAYOUT_LDFLAGS) -Wl,-T,$(REGIONS_LD) -Wl,-T,$(GAME_LD)
LDFLAGS_ALL := -m$(BITS) $(SANITIZERS) $(GBA_LDFLAGS)

# The sanitizer options every test runs under, set here so the environment
# cannot weaken them: leaks detected, no suppressions file, UBSan backtraces.
override SANITIZER_ENV := ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS= UBSAN_OPTIONS=print_stacktrace=1
ifeq ($(TSAN),1)
override SANITIZER_ENV := TSAN_OPTIONS=halt_on_error=1:exitcode=66
endif
# The one test binary that leaves a signal handler by siglongjmp, on purpose
# (test_arm_division_posix): ThreadSanitizer keeps believing the thread is in
# the handler and calls every later free() signal-unsafe. It runs with that
# report off (report_signal_unsafe=0); races are still reported, and every
# other binary keeps the report.
SIGNAL_JUMP_TESTS := test_arm_division_posix
# The same list as one shell case pattern (*/a|*/b), for the test recipe.
space := $(subst ,, )
SIGNAL_JUMP_PATTERN := $(subst $(space),|,$(addprefix */,$(SIGNAL_JUMP_TESTS)))
SIGNAL_JUMP_ENV := $(if $(filter 1,$(TSAN)),TSAN_OPTIONS=halt_on_error=1:exitcode=66:report_signal_unsafe=0)

PLATFORM_SRCS := $(filter-out host_%.c,$(wildcard *.c)) $(HOST_SRCS)
MODULE_SRCS := $(PLATFORM_SRCS) $(wildcard m4a/m4a_*.c)
MODULE_OBJS := $(MODULE_SRCS:%.c=$(OBJ_DIR)/%.o)
HOST_OBJS   := $(HOST_SRCS:%.c=$(OBJ_DIR)/%.o)
# The tests every target runs (test/test_*.c) and the tests only this OS can
# run (test/<dir>/test_*.c for each of host.mk's HOST_TEST_DIRS), whose binaries
# carry the directory's name as a suffix (a port adds its directory and its
# support, see README.md).
TEST_SRCS   := $(wildcard test/test_*.c) $(foreach dir,$(HOST_TEST_DIRS),$(wildcard test/$(dir)/test_*.c))
TEST_NAMES  := $(basename $(notdir $(TEST_SRCS)))
# test/support_<family>.c: what the tests share of the OS (test/test_support.h).
TEST_SUPPORT_OBJ := $(OBJ_DIR)/test/$(HOST_TEST_SUPPORT_DIR)/support.o
# test_crt0 reads where the linker put the game's objects, which ThreadSanitizer's
# instrumentation changes (crt0_init finds no game object): it is not run under it.
ifeq ($(TSAN),1)
TEST_NAMES := $(filter-out test_crt0,$(TEST_NAMES))
endif
# Linux runs each test binary where it is built; the Windows ones are .exe
# files in a directory of their own, with SDL2.dll beside them, to be copied
# to Windows whole.
ifeq ($(TARGET_OS),windows)
TEST_BIN_DIR := $(BUILD_DIR)/test
else
TEST_BIN_DIR := $(BUILD_DIR)
endif
TEST_BINS   := $(TEST_NAMES:%=$(TEST_BIN_DIR)/%$(TARGET_EXE_SUFFIX))
LIB         := $(BUILD_DIR)/libplatform.a

.PHONY: tsan all lib test clean format format-check lint lint-tidy lint-cppcheck gate
all: lib test

lib: $(LIB)

$(LIB): $(MODULE_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_ALL) -c $< -o $@

# The Windows executable's filler between the GBA regions (gba_regions.pe.ld
# says why): gba_gaps.pe.s assembled, then every `gap` section made read-only,
# which the assembler cannot say. A link of the library's users names it
# beside libplatform.a, since an archive member nothing refers to is not pulled.
ifeq ($(TARGET_OS),windows)
GAPS_OBJ := $(BUILD_DIR)/gba_gaps.o
GAP_SECTIONS := $(shell sed -n 's/^[[:space:]]*gap[[:space:]][[:space:]]*\(\.gap[0-9]*\),.*/\1/p' gba_gaps.pe.s)

lib: $(GAPS_OBJ)

$(GAPS_OBJ): gba_gaps.pe.s
	@mkdir -p $(@D)
	$(HOST_AS) -o $@ $<
	$(OBJCOPY) $(GAP_SECTIONS:%=--set-section-flags %=alloc,readonly) $@
endif

# test_arm_division divides by a global through an absolute address with an
# index and no base register (divl sym(,%ebx,4); on x86-64 sym(,%rbx,4) and a
# bare absolute sym), an operand form the game's -fno-pie code (Makefile.native)
# uses for its tables and that the SIGFPE decoder must handle; position-
# independent code cannot encode it.
$(OBJ_DIR)/test/test_arm_division.o: CFLAGS_ALL += -fno-pie

# These objects read expansion's structure layouts through its headers, so they
# are compiled as the game is (Makefile.native CPPFLAGS and ARCH_FLAGS), or the
# layouts could differ: inspect.c and its tests read the game's variables;
# save_convert.c pins its layout tables to the game's structs.
GAME_FLAG_OBJS := $(OBJ_DIR)/inspect.o $(OBJ_DIR)/inspect_menu.o $(OBJ_DIR)/inspect_args.o \
                  $(OBJ_DIR)/test/test_inspect_args.o $(OBJ_DIR)/save_convert.o
$(GAME_FLAG_OBJS): CFLAGS_ALL += $(GAME_DEFINES) -DTESTING=0 -DEMERALD $(GAME_CHAR_FLAGS)
# -MD, not -MMD: expansion's headers come by -idirafter, which makes them system
# headers, and -MMD leaves those out of the dependency file, so a change to a game
# header (include/task.h, ...) would leave these objects compiled against the old
# structure layouts.
$(GAME_FLAG_OBJS): CFLAGS_ALL := $(subst -MMD,-MD,$(CFLAGS_ALL))
# They also rebuild when the flags here change: an object compiled before a flag
# or dependency-tracking change would otherwise keep a stale layout (the .d files
# written with -MMD list no game header, so a game header change is not seen until
# the object is rebuilt once).
$(GAME_FLAG_OBJS): Makefile game_flags.mk

# The tests whose link also writes the binary's function map (host.mk's
# write_symbol_map), as the game's does: test_symbols reads its own.
SYMBOL_MAP_TESTS := test_symbols

# The Windows tests cannot run here, so each link's layout is checked from the
# file, as the game's is (Makefile.native).
ifeq ($(TARGET_OS),windows)
TEST_LINK_CHECK = python3 -I build_glue/check_pe_layout.py $(1) || { rm -f $(1); exit 1; }
endif

# A test's source: test/<name>.c or test/<family>/<name>.c.
test_source = $(filter %/$(1).c,$(TEST_SRCS))
# The files a test names on its "// deps:" line.
test_deps = $(shell sed -n -E 's@^//[[:space:]]*\#?[[:space:]]*deps:@@p' $(call test_source,$(1)))
# The module under test, if the test is named after one, plus its deps.
test_inputs = $(wildcard $(1:test_%=%).c) $(call test_deps,$(1))

define TEST_LINK_RULE
$(foreach dep,$(call test_deps,$(1)),$(if $(wildcard $(dep)),,$(error $(call test_source,$(1)) lists '$(dep)' on its deps line, but platform/$(dep) does not exist)))
$(TEST_BIN_DIR)/$(1)$(TARGET_EXE_SUFFIX): $(patsubst %.c,$(OBJ_DIR)/%.o,$(call test_source,$(1))) $(patsubst %.c,$(OBJ_DIR)/%.o,$(call test_inputs,$(1))) $(HOST_OBJS) $(TEST_SUPPORT_OBJ) $(GAPS_OBJ) $(call target_resources,$(OBJ_DIR)) $(REGIONS_LD) $(GAME_LD)
	@mkdir -p $$(@D)
	$$(CC) $$(LDFLAGS_ALL) -o $$@ $$(filter %.o,$$^) $$(SDL_LIBS)
	$$(if $$(TEST_LINK_CHECK),$$(call TEST_LINK_CHECK,$$@))
	$$(if $$(filter $$(SYMBOL_MAP_TESTS),$(1)),$$(call write_symbol_map,$$@,$$@.symbols) || { rm -f $$@; exit 1; })
endef
$(foreach name,$(TEST_NAMES),$(eval $(call TEST_LINK_RULE,$(name))))

ifeq ($(TARGET_OS),windows)
# Built, not run: test_runner/run_windows_tests.sh runs them on Windows, with
# the same checks as below but the sanitizers'.
$(TEST_BIN_DIR)/SDL2.dll: $(SDL2_MINGW_DIR)/bin/SDL2.dll
	@mkdir -p $(@D)
	cp $< $@

test: $(TEST_BINS) $(TEST_BIN_DIR)/SDL2.dll
	@echo "built $(words $(TEST_BINS)) test executables in platform/$(TEST_BIN_DIR); on Windows: platform/test_runner/run_windows_tests.sh platform/$(TEST_BIN_DIR)"
else
# Runs every test even after a failure, then fails if any test exited non-zero,
# printed a sanitizer report (a report that did not change the exit status
# still fails the run), or ended without ptest's "N passed, 0 failed" line (a
# binary that exits early, from a test, passes no test).
test: $(TEST_BINS)
	@status=0; \
	for bin in $(TEST_BINS); do \
		echo "== $$bin"; \
		extra=; case $$bin in $(SIGNAL_JUMP_PATTERN)) extra='$(SIGNAL_JUMP_ENV)';; esac; \
		env $(SANITIZER_ENV) $$extra ./$$bin > $$bin.log 2>&1; code=$$?; \
		cat $$bin.log; \
		if [ $$code -ne 0 ]; then \
			echo "FAILED: $$bin exited with status $$code"; status=1; \
		elif grep -qE 'Sanitizer|runtime error:' $$bin.log; then \
			echo "FAILED: $$bin printed a sanitizer report"; status=1; \
		elif ! grep -qE '^[0-9]+ passed, 0 failed$$' $$bin.log; then \
			echo "FAILED: $$bin ended without its summary line"; status=1; \
		fi; \
	done; \
	if [ $$status -eq 0 ]; then echo "all platform tests passed (BITS=$(BITS))"; fi; \
	exit $$status
endif

clean:
	rm -rf build

# ---- style and static analysis (docs/C-RULES.md) ----------------------------

# Every C file the platform layer owns: modules, the m4a engine and its
# difftest, arm_division's libgcc record, tests and their headers. The build's
# own output is not ours.
FORMAT_FILES := $(shell find . -path ./build -prune -o \( -name '*.c' -o -name '*.h' \) -print | sort)
# The host layer's sources and the per-OS tests of the target OS only: the
# Windows ones are linted by `make lint TARGET_OS=windows BITS=64` (clang-tidy
# for the MinGW-w64 target, with its headers), not in the gate, which runs on
# Linux alone. clang-format covers every file (FORMAT_FILES).
# The m4a difftest and arm_division's libgcc record are Linux tools (run under
# qemu-arm, docs/UPGRADING.md), so the Windows lint leaves them out.
LINT_TOOL_SRCS := $(wildcard m4a/difftest/*.c) $(wildcard arm_division_libgcc/*.c)
ifeq ($(TARGET_OS),windows)
LINT_TOOL_SRCS :=
endif
LINT_SRCS    := $(PLATFORM_SRCS) $(wildcard m4a/*.c) $(LINT_TOOL_SRCS) \
                $(wildcard test/*.c) $(foreach dir,$(HOST_TEST_DIRS),$(wildcard test/$(dir)/*.c))

# A missing tool must fail the target, not pass it silently.
require_tool = @command -v $(1) >/dev/null 2>&1 || { echo "$(1) is not installed ($(2))"; exit 1; }

format:
	$(call require_tool,clang-format,make format)
	@clang-format -i $(FORMAT_FILES)
	@echo "format: $(words $(FORMAT_FILES)) files"

# --dry-run -Werror reports every violation and exits non-zero; the file list
# comes first so a failure says which files to `make format`.
format-check:
	$(call require_tool,clang-format,make format-check)
	@bad=$$(for f in $(FORMAT_FILES); do clang-format --dry-run -Werror $$f >/dev/null 2>&1 || echo $$f; done); \
	if [ -n "$$bad" ]; then echo "format-check: these files need 'make -C platform format':"; \
		echo "$$bad"; exit 1; fi; \
	echo "format-check: $(words $(FORMAT_FILES)) files clean"

# clang-tidy reads a compilation database with the build's flags (without the
# sanitizers and -Werror, which say nothing about the source): -m$(BITS), the
# same -I and -idirafter as the build, and the game's defines for the files
# that include expansion's headers as the game does. Expansion's headers are
# not ours: ../.clang-tidy's HeaderFilterRegex keeps findings to platform/.
LINT_DIR := $(BUILD_DIR)/lint
LINT_GAME_FILES := inspect.c inspect_menu.c inspect_args.c test/test_inspect_args.c save_convert.c
ifeq ($(TARGET_OS),windows)
LINT_TARGET_FLAGS := --target=x86_64-w64-mingw32 $(TARGET_CFLAGS)
endif
lint_flags = -m$(BITS) -std=gnu17 $(LINT_TARGET_FLAGS) $(INCLUDES) $(LARGE_TYPES) $(SDL_CFLAGS) $(if $(filter m4a/difftest/%,$(1)),-Im4a -Im4a/difftest) $(if $(filter $(1),$(LINT_GAME_FILES)),$(GAME_DEFINES) -DTESTING=0 -DEMERALD $(GAME_CHAR_FLAGS))
LINT_JOBS ?= $(shell nproc)

$(LINT_DIR)/compile_commands.json: Makefile game_flags.mk $(LINT_SRCS)
	@mkdir -p $(@D)
	@{ echo '['; \
	first=1; \
	$(foreach f,$(LINT_SRCS),[ $$first = 1 ] || echo ','; first=0; \
		printf '{"directory": "%s", "file": "%s", "command": "cc %s -c %s"}' "$(CURDIR)" "$(f)" "$(call lint_flags,$(f))" "$(f)"; ) \
	echo; echo ']'; } > $@

# One clang-tidy process per file, LINT_JOBS at a time, each with its own log
# (parallel output would interleave); findings in headers repeat in every file
# that includes them, so the combined report is sorted and de-duplicated.
lint-tidy: $(LINT_DIR)/compile_commands.json
	$(call require_tool,clang-tidy,make lint)
	@echo "clang-tidy: $(words $(LINT_SRCS)) files"
	@rm -rf $(LINT_DIR)/logs && mkdir -p $(LINT_DIR)/logs
	@printf '%s\n' $(LINT_SRCS) | xargs -P $(LINT_JOBS) -I{} sh -c \
		'f=$$(echo {} | tr / _); clang-tidy -quiet -p $(LINT_DIR) {} > $(LINT_DIR)/logs/$$f.log 2>&1; echo $$? > $(LINT_DIR)/logs/$$f.status'
	@cat $(LINT_DIR)/logs/*.log | grep -E '^[^ ]+:[0-9]+:[0-9]+: (warning|error): ' | sort -u > $(LINT_DIR)/clang-tidy.log; \
	broken=""; \
	for s in $(LINT_DIR)/logs/*.status; do \
		l=$${s%.status}.log; \
		if [ "$$(cat $$s)" != 0 ] && ! grep -qE '^[^ ]+:[0-9]+:[0-9]+: (warning|error): ' $$l; then broken="$$broken $$l"; fi; \
	done; \
	if [ -n "$$broken" ]; then echo "clang-tidy failed without a finding; see:$$broken"; exit 1; fi; \
	if [ "$$(ls $(LINT_DIR)/logs/*.status | wc -l)" -ne $(words $(LINT_SRCS)) ]; then echo "clang-tidy: not every file ran"; exit 1; fi; \
	if [ -s $(LINT_DIR)/clang-tidy.log ]; then cat $(LINT_DIR)/clang-tidy.log; \
		echo "clang-tidy: $$(wc -l < $(LINT_DIR)/clang-tidy.log) findings (full text in $(LINT_DIR)/logs)"; exit 1; fi; \
	echo "clang-tidy: clean"

# cppcheck has no system-include notion, so it is given expansion's headers
# with -I ../include (the build's -idirafter; with -I .. alone <gba/...> and
# "global.h" do not resolve, checked with --debug-warnings) and the suppression
# keeps their findings out. The missing standard headers are cppcheck's own
# builtin knowledge, hence missingIncludeSystem.
lint-cppcheck:
	$(call require_tool,cppcheck,make lint)
	cppcheck --std=c11 --enable=warning,style,performance,portability --inline-suppr -I . -I ../include \
		--suppress='*:../include/*' --suppress=missingIncludeSystem --error-exitcode=1 -q -j $(LINT_JOBS) $(LINT_SRCS)

lint: lint-tidy lint-cppcheck

# tools/patchelf on ELF32 and ELF64 (builds the tool if needed), and the
# Windows runner's scripts driven with fake executables, which run here; with
# TARGET_OS=windows, patchelf on a PE file built with MinGW-w64 instead (the
# Windows workflow's Linux job runs it: the platform gate runs on Linux alone).
tools-test:
	$(MAKE) -C .. -f make_tools.mk check-tools
ifeq ($(TARGET_OS),windows)
	test_runner/test_patchelf_pe.sh
else
	test_runner/test_patchelf.sh
	test_runner/test_run_windows_tests.sh
	test_runner/test_native_rom_test_windows.sh
	test_runner/test_compare_suite_results.sh
endif

.PHONY: tools-test

# The self-test of every build glue script and module (each takes --self-test);
# Makefile.native runs the ones the 64-bit link needs, this runs them all. The
# object-file readers' self-tests assemble fixtures with `as` and compile them
# with `clang` for the ELF, Mach-O and COFF targets.
GLUE_SCRIPTS := $(sort $(wildcard build_glue/*.py))
glue-test:
	$(call require_tool,clang,make glue-test)
	$(call require_tool,as,make glue-test)
	@set -e; for script in $(GLUE_SCRIPTS); do \
		echo "python3 -I $$script --self-test"; python3 -I $$script --self-test; \
	done

.PHONY: glue-test

# The platform gate: style, analysis, then the sanitized unit tests at both widths.
# Not in it, because they need qemu-arm and the ARM toolchain: the records
# against the GBA's own code, run by hand (docs/UPGRADING.md):
# `make -C platform/m4a/difftest run` and `make -C platform/arm_division_libgcc run`.
tsan:
	$(MAKE) test BITS=64 TSAN=1

gate:
	$(MAKE) format-check
	$(MAKE) lint BITS=32
	$(MAKE) lint BITS=64
	$(MAKE) test BITS=32
	$(MAKE) test BITS=64
	$(MAKE) tsan
	$(MAKE) tools-test
	$(MAKE) glue-test

-include $(wildcard $(OBJ_DIR)/*.d $(OBJ_DIR)/test/*.d $(OBJ_DIR)/test/*/*.d $(OBJ_DIR)/m4a/*.d)
