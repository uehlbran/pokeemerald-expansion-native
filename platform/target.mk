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
#   CONSOLE_LDFLAGS, GUI_LDFLAGS   the subsystem of an executable: the unit
#                        tests are console programs (Windows: -mconsole), the
#                        game is a GUI program, so that Explorer opens no
#                        console window beside it (Windows: -mwindows; Linux:
#                        neither has a flag)
#   $(call target_resources,DIR)   the resource objects every executable
#                        links, made in DIR by this file's rule (Windows: the
#                        application manifest; Linux: none)
# and, for windows, the cross tools (TARGET_CC, TARGET_AS, NM, CC, AR, OBJCOPY, OBJDUMP),
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
CONSOLE_LDFLAGS :=
GUI_LDFLAGS :=
REGIONS_LD_NAME := gba_regions.ld
GAME_LD_NAME := gba_game.ld
target_resources =

else ifeq ($(TARGET_OS),windows)

ifneq ($(BITS),64)
$(error TARGET_OS=windows is 64-bit only (BITS=64), not '$(BITS)': an i686 COFF object prefixes every C symbol with an underscore, which the linker scripts and the assembled data would need a second spelling for (research/m8-inventory.md 2.3))
endif

CROSS_PREFIX := x86_64-w64-mingw32-
TARGET_CC ?= $(CROSS_PREFIX)gcc
TARGET_AS ?= $(CROSS_PREFIX)as
NM      ?= $(CROSS_PREFIX)nm
OBJCOPY ?= $(CROSS_PREFIX)objcopy
OBJDUMP ?= $(CROSS_PREFIX)objdump
# platform/Makefile compiles with $(CC) and archives with $(AR), which make
# defaults to the build machine's.
CC := $(TARGET_CC)
AR := $(CROSS_PREFIX)ar

TARGET_EXE_SUFFIX := .exe

# -mno-ms-bitfields: MinGW's GCC lays bit-fields out as Microsoft's compiler
# does unless told otherwise; expansion has 1,247 bit-field members, and the
# save file is those structs' bytes, so they keep the layout GCC gives them on
# Linux and a save moves between the two builds (docs/ARCHITECTURE.md, "The
# save container"). -D__USE_MINGW_ANSI_STDIO=1: MinGW's printf family is the C
# runtime's, which does not know %zu or %llx the C99 way; this one is MinGW's
# own. Win64 `long` is 32 bits: nothing here assumes otherwise. -idirafter:
# build_glue/mingw_include has the headers MinGW lacks that expansion includes.
# -Wa,--keep-locals: GNU as for x86-64 COFF takes every name that starts with
# "L" for an assembler-local label (bfd's coff-x86_64.c) and leaves it out of
# the object, so the game's static functions named so (ListMenuDummyTask,
# LaunchKOAnimation and 182 more) had no name in the executable's symbol table
# and its function map, from which the replays name callbacks (symbols.h): the
# debug-menu replay's task lists showed "unknown:0x..." where Linux names
# them. GCC's own labels are ".L"; the link drops them again.
TARGET_CFLAGS := -mno-ms-bitfields -D__USE_MINGW_ANSI_STDIO=1 -Wa,--keep-locals \
                 -idirafter $(abspath $(TARGET_MK_DIR)build_glue/mingw_include)

# The SDL2 development package for MinGW, fetched by Makefile.native
# (sdl2-mingw) into build/sdl2-mingw/. Used directly, not through its
# sdl2.pc: that file carries the prefix the package was built under, adds
# -Dmain=SDL_main, -lSDL2main and -mwindows (a GUI executable with SDL's
# WinMain), and this one has its own main(), so SDL_MAIN_HANDLED (platform/main.c)
# and the subsystem set below.
SDL2_MINGW_DIR := $(abspath $(TARGET_MK_DIR)../build/sdl2-mingw)/x86_64-w64-mingw32
SDL_CFLAGS := -isystem $(SDL2_MINGW_DIR)/include/SDL2 -DSDL_MAIN_HANDLED
SDL_LIBS   := -L$(SDL2_MINGW_DIR)/lib -lSDL2

# The layout the Windows loader accepted (research/probes/README.md, probe W1):
# the image at 0x01800000, below the lowest region; no address randomisation
# and no base relocations, so that the loader maps the image at its base or
# refuses it; .text at 0x40001000, where the Linux build's code is (the
# headers take the first page of the image at 0x40000000 there).
# gba_regions.pe.ld puts the regions and the filled gaps between.
GBA_LAYOUT_LDFLAGS := -static-libgcc -Wl,--image-base=0x01800000 -Wl,--disable-dynamicbase \
                      -Wl,--disable-high-entropy-va -Wl,--disable-reloc-section \
                      -Wl,--section-start=.text=0x40001000
REGIONS_LD_NAME := gba_regions.pe.ld
GAME_LD_NAME := gba_game.pe.ld

# The game is the "Windows GUI" subsystem and keeps its own main: MinGW-w64's
# start-up code for -mwindows (WinMainCRTStartup) calls main with the
# command line it parsed, as the console one does. The subsystem is the only
# difference to the loader: it creates no console for the process, and the
# C runtime takes stdout and stderr from the handles the parent gave it, if
# any (platform/console.h says what the game does with none). The unit tests
# stay console programs, so that run_windows_tests.sh shows their output.
CONSOLE_LDFLAGS := -mconsole
GUI_LDFLAGS := -mwindows

# The application manifest that makes the process's code page UTF-8, linked
# into the game and into every test executable alike, so that the narrow
# strings the C runtime and the -A functions take and give (argv, paths) are
# UTF-8 as on Linux (build_glue/windows_manifest.xml; docs/DECISIONS.md,
# "Narrow strings on Windows are UTF-8"). windres compiles the resource
# script; a pattern rule, so that each Makefile names the object in its own
# build directory and none of them takes it for its default goal.
WINDRES ?= $(CROSS_PREFIX)windres
TARGET_MANIFEST_DIR := $(abspath $(TARGET_MK_DIR)build_glue)
target_resources = $(1)/windows_manifest.o

%/windows_manifest.o: $(TARGET_MANIFEST_DIR)/windows_manifest.rc $(TARGET_MANIFEST_DIR)/windows_manifest.xml
	@mkdir -p $(@D)
	$(WINDRES) --include-dir $(TARGET_MANIFEST_DIR) -O coff -o $@ $<

endif
