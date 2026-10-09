# The record of libgcc's division results on the GBA, which
# platform/arm_division.c reproduces natively.
#
#   make -C platform/arm_division_libgcc run
#
# builds libgcc_division.c for the ARM7TDMI and runs it under qemu-arm
# (bare-metal newlib with semihosting, as platform/m4a/difftest), then
# compares its output with the checked-in record, libgcc_division.txt.
# `make record` rewrites the record instead. Needs arm-none-eabi-gcc with
# newlib and qemu-arm.
#
# The operators (libgcc_division_ops.c) are compiled twice: with the flags
# expansion's Makefile compiles src/ with (Thumb, -mabi=apcs-gnu), so they
# call what the game's code calls; and for the EABI (-mabi=aapcs), so they
# call the __aeabi_* routines. Both are linked against the libgcc the GBA
# build links, the one `arm-none-eabi-gcc -mthumb -print-file-name=libgcc.a`
# names (Makefile, LIBPATH); build/libgcc_division.map shows which routines
# came from it.

BUILD   := build
RECORD  := libgcc_division.txt

ARM_CC  := arm-none-eabi-gcc
ARM_CPU := -mthumb -mthumb-interwork -mtune=arm7tdmi -march=armv4t
# expansion's Makefile: CFLAGS for src/ (the code-generation part), and
# its ASFLAGS' -meabi=5, with which it assembles cc1's output.
GAME_CFLAGS := $(ARM_CPU) -O2 -mabi=apcs-gnu -std=gnu17 -Wa,-meabi=5
EABI_CFLAGS := $(ARM_CPU) -O2 -mabi=aapcs -std=gnu17
WARNINGS    := -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion -Werror
LIBGCC_DIR  := $(dir $(shell $(ARM_CC) -mthumb -print-file-name=libgcc.a))
LDFLAGS     := $(ARM_CPU) --specs=rdimon.specs -L$(LIBGCC_DIR) -Wl,-Map=$(BUILD)/libgcc_division.map

.PHONY: run record clean

run: $(BUILD)/libgcc_division.txt
	cmp $(BUILD)/libgcc_division.txt $(RECORD)
	@echo "libgcc_division: qemu-arm output equals $(RECORD)"

record: $(BUILD)/libgcc_division.txt
	cp $< $(RECORD)

$(BUILD)/libgcc_division.txt: $(BUILD)/libgcc_division.elf
	qemu-arm $< > $@

$(BUILD)/ops_game.o: libgcc_division_ops.c libgcc_division.h | $(BUILD)
	$(ARM_CC) $(GAME_CFLAGS) $(WARNINGS) -DDIVISION_OPS_NAME=game_division_ops -c $< -o $@

$(BUILD)/ops_eabi.o: libgcc_division_ops.c libgcc_division.h | $(BUILD)
	$(ARM_CC) $(EABI_CFLAGS) $(WARNINGS) -DDIVISION_OPS_NAME=eabi_division_ops -c $< -o $@

$(BUILD)/libgcc_division.o: libgcc_division.c libgcc_division.h | $(BUILD)
	$(ARM_CC) $(EABI_CFLAGS) $(WARNINGS) -c $< -o $@

$(BUILD)/libgcc_division.elf: $(BUILD)/libgcc_division.o $(BUILD)/ops_game.o $(BUILD)/ops_eabi.o
	$(ARM_CC) $^ $(LDFLAGS) -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
