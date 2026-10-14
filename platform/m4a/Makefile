# Standalone build and unit tests of the m4a engine, the C translation of
# src/m4a_1.s. platform/Makefile builds the same files into libplatform.a
# and runs the same tests; this file builds only the m4a part:
#
#   make -C platform/m4a test          build and run the m4a tests (BITS=32)
#   make -C platform/m4a test BITS=64  the same for a 64-bit build
#   make -C platform/m4a clean
#
# Flags follow platform/Makefile, which explains each choice: gnu17, the
# warning set as errors for our files, -O1 -g, ASan, UBSan and LSan on every
# object and every run, expansion's headers through -idirafter, and tests
# linked non-PIE with gba_regions.ld so the GBA regions (IWRAM with
# SOUND_INFO_PTR, the I/O registers) exist at their GBA addresses. The tests
# live in platform/test/test_m4a_*.c and also link expansion's
# src/m4a_tables.c, the engine's data tables.

BITS ?= 32
ifeq ($(filter $(BITS),32 64),)
$(error BITS must be 32 or 64, not '$(BITS)')
endif

PLATFORM := ..
ROOT     := ../..
BUILD    := build/$(BITS)

WARNINGS := -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion -Werror
override SANITIZERS := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS   := -m$(BITS) -std=gnu17 $(WARNINGS) -O1 -g $(SANITIZERS) -I. -I$(PLATFORM) \
            -idirafter $(ROOT)/include -MMD -MP
REGIONS_LD := $(PLATFORM)/gba_regions.ld
LDFLAGS  := -m$(BITS) $(SANITIZERS) -no-pie -Wl,-Ttext-segment=0x40000000 -Wl,-T,$(REGIONS_LD)
override SANITIZER_ENV := ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS= UBSAN_OPTIONS=print_stacktrace=1

ENGINE_SRCS  := $(wildcard m4a_*.c)
SUPPORT_SRCS := test_m4a_support.c test_m4a_c_reference.c
TEST_SRCS    := $(wildcard $(PLATFORM)/test/test_m4a_*.c)
TEST_BINS    := $(TEST_SRCS:$(PLATFORM)/test/%.c=$(BUILD)/%)

# test_m4a_output.c tests ../m4a_output.c, which reads the engine's SoundInfo
# through ../gba_memmap.c's regions; gba_memmap.c and the tests' support find
# the regions, and map the low page, through the host layer (../host.mk).
include $(PLATFORM)/host.mk
PLATFORM_SRCS := m4a_output.c gba_memmap.c $(HOST_SRCS)

COMMON_OBJS := $(ENGINE_SRCS:%.c=$(BUILD)/obj/%.o) $(SUPPORT_SRCS:%.c=$(BUILD)/obj/%.o) \
               $(PLATFORM_SRCS:%.c=$(BUILD)/obj/platform/%.o) $(BUILD)/obj/m4a_tables.o

.PHONY: all test clean

all: $(TEST_BINS)

# Runs every test binary, then fails if any exited non-zero, printed a
# sanitizer report, or ended without ptest's "N passed, 0 failed" line.
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
		elif ! grep -qE '^[0-9]+ passed, 0 failed$$' $$bin.log; then \
			echo "FAILED: $$bin ended without its summary line"; status=1; \
		fi; \
	done; \
	if [ $$status -eq 0 ]; then echo "all m4a tests passed (BITS=$(BITS))"; fi; \
	exit $$status

$(TEST_BINS): $(BUILD)/%: $(BUILD)/obj/test/%.o $(COMMON_OBJS) $(REGIONS_LD)
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^)

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/test/%.o: $(PLATFORM)/test/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/platform/%.o: $(PLATFORM)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/m4a_tables.o: $(ROOT)/src/m4a_tables.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

-include $(wildcard $(BUILD)/obj/*.d $(BUILD)/obj/test/*.d)
