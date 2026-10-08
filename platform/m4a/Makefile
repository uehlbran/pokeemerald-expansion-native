# Standalone build and unit tests of the m4a engine, the C translation of
# src/m4a_1.s. Run from this directory: `make test`. To be folded into
# platform/Makefile.
#
# Our files build with the project's warning set as errors. Expansion's
# headers are system headers so their warnings are not ours to fix
# (docs/C-RULES.md). They come in with -idirafter rather than -isystem:
# -isystem searches them before libc's, and expansion's include/strings.h
# then shadows the <strings.h> that glibc's <string.h> includes;
# -idirafter marks them as system headers too but searches them last. The tests also link expansion's src/m4a_tables.c, the
# engine's data tables (gClockTable, gScaleTable, gFreqTable,
# gDeltaEncodingTable, gMPlayJumpTableTemplate), which builds cleanly with
# the same flags. Every test binary runs under AddressSanitizer,
# UndefinedBehaviorSanitizer and LeakSanitizer; any report fails the run.

CC      := gcc
ROOT    := ../..
BUILD   := build
ARCH    := -m32
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all
CFLAGS  := $(ARCH) -std=gnu17 -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion -Werror \
           -O1 -g $(SAN) -idirafter $(ROOT)/include -I.
LDFLAGS := $(ARCH) $(SAN)

ENGINE_SRCS  := m4a_mixer.c m4a_sequencer.c m4a_commands.c m4a_clear64.c
SUPPORT_SRCS := test_m4a_support.c
TESTS        := test_m4a_engine test_m4a_mixer

ENGINE_OBJS  := $(ENGINE_SRCS:%.c=$(BUILD)/%.o)
COMMON_OBJS  := $(ENGINE_OBJS) $(SUPPORT_SRCS:%.c=$(BUILD)/%.o) $(BUILD)/m4a_tables.o
TEST_BINS    := $(TESTS:%=$(BUILD)/%)
HEADERS      := $(wildcard *.h)

.PHONY: all test clean

all: $(TEST_BINS)

# Runs every test binary, then fails if any of them failed.
test: $(TEST_BINS)
	@status=0; for t in $(TEST_BINS); do \
		echo "== $$t"; \
		ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./$$t || status=1; \
	done; exit $$status

$(TEST_BINS): $(BUILD)/%: $(BUILD)/%.o $(COMMON_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/%.o: %.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/m4a_tables.o: $(ROOT)/src/m4a_tables.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
