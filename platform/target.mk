# platform/target.mk: the operating system the native executable is built for,
# and what follows from it for the toolchain. Included by host.mk, so by every
# Makefile that builds platform code (platform/Makefile, platform/m4a/Makefile,
# Makefile.native), after BITS is set.
#
# TARGET_OS is where the executable runs: the machine building it (HOST_OS,
# host.mk) by default, or `windows`, a cross build from Linux with MinGW-w64
# (docs/BUILDING.md, "Windows (cross-build from Linux)"). Nothing else is
# accepted. Windows is x86_64 only: an i686 COFF object prefixes every C
# symbol with an underscore, which would need a second spelling in the two
# linker scripts and in every global the assembled data defines or uses, for a
# target Windows 11 no longer ships (docs/DECISIONS.md, "The ports share a thin
# host layer"; research/m8-inventory.md 2.3).
#
# What this file defines, for the others to use:
#   TARGET_EXE_SUFFIX    the executable's suffix
#   GBA_LAYOUT_LDFLAGS   the link flags that put the image where gba_regions.ld
#                        and gba_game.ld expect it
#   REGIONS_LD_NAME, GAME_LD_NAME   the file names of those two linker scripts
#                        (in platform/)
#   SDL_CFLAGS, SDL_LIBS how to compile and link against SDL2
# and, for windows, the cross tools (HOST_CC, HOST_AS, NM, CC, AR, OBJDUMP),
# TARGET_CFLAGS (the compiler flags every Windows object takes) and the SDL2
# development package's location (SDL2_MINGW_DIR; Makefile.native fetches it).
# The Linux values are what the Makefiles spelled out before this file.

# Evaluated now, while this file is still the last one included.
TARGET_MK_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

TARGET_OS ?= $(HOST_OS)
ifeq (,$(filter Linux windows,$(TARGET_OS)))
$(error TARGET_OS must be Linux (the default) or windows, not '$(TARGET_OS)')
endif

ifeq ($(TARGET_OS),Linux)

TARGET_EXE_SUFFIX :=
TARGET_CFLAGS :=
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
# Non-PIE, the image at 0x40000000, clear of every region on both bit widths
# (docs/ARCHITECTURE.md, "The memory model").
GBA_LAYOUT_LDFLAGS := -no-pie -Wl,-Ttext-segment=0x40000000
REGIONS_LD_NAME := gba_regions.ld
GAME_LD_NAME := gba_game.ld

else ifeq ($(TARGET_OS),windows)

ifneq ($(BITS),64)
$(error TARGET_OS=windows is 64-bit only (BITS=64), not '$(BITS)': an i686 COFF object prefixes every C symbol with an underscore, which the linker scripts and the assembled data would need a second spelling for (research/m8-inventory.md 2.3))
endif

CROSS_PREFIX := x86_64-w64-mingw32-
HOST_CC ?= $(CROSS_PREFIX)gcc
HOST_AS ?= $(CROSS_PREFIX)as
NM      ?= $(CROSS_PREFIX)nm
OBJDUMP ?= $(CROSS_PREFIX)objdump
# platform/Makefile compiles with $(CC) and archives with $(AR), which make
# defaults to the build machine's.
CC := $(HOST_CC)
AR := $(CROSS_PREFIX)ar

TARGET_EXE_SUFFIX := .exe

# -mno-ms-bitfields: MinGW's GCC lays bit-fields out as Microsoft's compiler
# does unless told otherwise; expansion has 1,247 bit-field members, and the
# save file is those structs' bytes, so they keep the layout GCC gives them on
# Linux and a save moves between the two builds (docs/ARCHITECTURE.md, "The
# save container"). -D__USE_MINGW_ANSI_STDIO=1: MinGW's printf family is the C
# runtime's, which does not know %zu or %llx the C99 way; this one is MinGW's
# own. Win64 `long` is 32 bits: nothing here assumes otherwise.
TARGET_CFLAGS := -mno-ms-bitfields -D__USE_MINGW_ANSI_STDIO=1

# The SDL2 development package for MinGW, fetched by Makefile.native
# (sdl2-mingw) into build/sdl2-mingw/. Used directly, not through its
# sdl2.pc: that file carries the prefix the package was built under, adds
# -Dmain=SDL_main, -lSDL2main and -mwindows (a GUI executable with SDL's
# WinMain), and this one is a console executable with its own main(), so
# SDL_MAIN_HANDLED and -mconsole (platform/main.c).
SDL2_MINGW_DIR := $(abspath $(TARGET_MK_DIR)../build/sdl2-mingw)/x86_64-w64-mingw32
SDL_CFLAGS := -isystem $(SDL2_MINGW_DIR)/include/SDL2 -DSDL_MAIN_HANDLED
SDL_LIBS   := -L$(SDL2_MINGW_DIR)/lib -lSDL2

# The layout the Windows loader accepted (research/probes/README.md, probe W1):
# the image at 0x01800000, below the lowest region; no address randomisation
# and no base relocations, so that the loader maps the image at its base or
# refuses it; .text at 0x40001000, where the Linux build's code is (the
# headers take the first page of the image at 0x40000000 there).
# gba_regions.pe.ld puts the regions and the filled gaps between. The
# __executable_start alias: src/malloc.c measures the heap's allocation sites
# from it, GNU ld defines it for ELF, and on PE the image starts at __ImageBase.
GBA_LAYOUT_LDFLAGS := -mconsole -static-libgcc -Wl,--image-base=0x01800000 -Wl,--disable-dynamicbase \
                      -Wl,--disable-high-entropy-va -Wl,--disable-reloc-section \
                      -Wl,--section-start=.text=0x40001000 \
                      -Wl,--defsym,__executable_start=__ImageBase
REGIONS_LD_NAME := gba_regions.pe.ld
GAME_LD_NAME := gba_game.pe.ld

endif
