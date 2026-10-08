# Differential test of the m4a engine against the original assembly.
#
#   make -C platform/m4a/difftest run
#
# builds difftest.c twice: for ARM with expansion's src/m4a_1.s (bare-metal
# newlib with semihosting, run under qemu-arm user mode; the GBA pages the
# engine addresses are sections linked at their GBA addresses), and natively
# (32-bit, ASan and UBSan) with the C translation. The two digests must be
# identical. Needs arm-none-eabi-gcc with newlib and qemu-arm.

ROOT     := ../../..
ENGINE   := ..
BUILD    := build

CC       := gcc
CFLAGS   := -m32 -std=gnu17 -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion -Werror -O1 -g \
            -fsanitize=address,undefined -fno-sanitize-recover=all -idirafter $(ROOT)/include -I. -I$(ENGINE)
LDFLAGS  := -m32 -fsanitize=address,undefined

ARM_CC      := arm-none-eabi-gcc
ARM_AS      := arm-none-eabi-as
ARM_CFLAGS  := -mcpu=arm7tdmi -marm -mthumb-interwork -std=gnu17 -O1 -g -idirafter $(ROOT)/include -I. -I$(ENGINE)
ARM_LDFLAGS := --specs=rdimon.specs -Wl,-Ttext=0x08000000 \
               -Wl,--section-start=.gba_iwram=0x03007000 -Wl,--section-start=.gba_io=0x04000000

DRIVER_SRCS := difftest.c difftest_m4a_c.c difftest_platform.c $(ENGINE)/test_m4a_c_reference.c $(ROOT)/src/m4a_tables.c
ENGINE_SRCS := $(addprefix $(ENGINE)/,m4a_mixer.c m4a_render.c m4a_sequencer.c m4a_note.c m4a_commands.c m4a_clear64.c)

.PHONY: run clean

run: $(BUILD)/difftest_arm.elf $(BUILD)/difftest_native
	qemu-arm $(BUILD)/difftest_arm.elf > $(BUILD)/arm.txt
	ASAN_OPTIONS=detect_leaks=1 ./$(BUILD)/difftest_native > $(BUILD)/native.txt
	cmp $(BUILD)/arm.txt $(BUILD)/native.txt
	@echo "difftest: $$(grep -c '^F' $(BUILD)/arm.txt) frames identical"

$(BUILD)/m4a_1.o: $(ROOT)/src/m4a_1.s | $(BUILD)
	$(ARM_AS) -mcpu=arm7tdmi -I $(ROOT) -I $(ROOT)/include -o $@ $<

$(BUILD)/difftest_arm.elf: $(DRIVER_SRCS) $(BUILD)/m4a_1.o | $(BUILD)
	$(ARM_CC) $(ARM_CFLAGS) $^ $(ARM_LDFLAGS) -o $@

$(BUILD)/difftest_native: $(DRIVER_SRCS) $(ENGINE_SRCS) $(wildcard *.h) $(wildcard $(ENGINE)/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(DRIVER_SRCS) $(ENGINE_SRCS) $(LDFLAGS) -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
