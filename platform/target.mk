# platform/target.mk: the operating system the native executable is built for,
# and what follows from it for the toolchain. Included by host.mk, so by every
# Makefile that builds platform code (platform/Makefile, platform/m4a/Makefile,
# Makefile.native), after BITS is set.
#
# TARGET_OS is where the executable runs: the machine building it (HOST_OS,
# host.mk) by default, Linux or Darwin (macOS, built on a Mac:
# docs/BUILDING.md, "macOS"), or `windows`, a cross build from Linux with
# MinGW-w64 (docs/BUILDING.md, "Windows (cross-build from Linux)"). Nothing
# else is accepted. Windows is x86_64 only: an i686 COFF object prefixes every
# C symbol with an underscore, which would need a second spelling in the two
# linker scripts and in every global the assembled data defines or uses, for a
# target Windows 11 no longer ships (docs/DECISIONS.md, "The ports share a thin
# host layer"). macOS is x86_64 only too: arm64 cannot load the
# fixed-address image (docs/DECISIONS.md, "macOS ships as x86_64 under
# Rosetta").
#
# What this file defines, for the others to use:
#   TARGET_EXE_SUFFIX    the executable's suffix
#   GBA_LAYOUT_LDFLAGS   the link flags that put the image where gba_regions.ld
#                        and gba_game.ld expect it
#   GBA_SANITIZED_LAYOUT_LDFLAGS   added for the sanitized game and test runner
#                        (Makefile.native, SANITIZE=1), whose read-only data
#                        passes 0x42000000: their writable data starts at
#                        0x43000000, clear of the addresses a GBA test of bits
#                        24-27 takes for EWRAM (gba_regions.ld says why)
#   REGIONS_LD_NAME, GAME_LD_NAME   the file names of those two linker scripts
#                        (in platform/), and GBA_LINKER_SCRIPT_FLAG, the flag
#                        that hands the linker each (GNU ld: -T; macOS's ld64,
#                        which has no scripts: -alias_list, two lists of the
#                        names the scripts would define)
#   GBA_LAYOUT_FIRST, GBA_LAYOUT_LAST   assembly sources in platform/ that every
#                        executable links before and after all its other
#                        objects (macOS: the regions, gba_regions.macho.s says
#                        why their place matters; elsewhere none)
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
ifeq (,$(filter Linux Darwin windows,$(TARGET_OS)))
$(error TARGET_OS must be Linux or Darwin (the system building, the default) or windows, not '$(TARGET_OS)')
endif
ifeq ($(TARGET_OS)/$(HOST_OS),Darwin/Linux)
$(error TARGET_OS=Darwin builds on a Mac only (HOST_OS Darwin): docs/BUILDING.md, "macOS")
endif

GBA_LINKER_SCRIPT_FLAG := -Wl,-T,
GBA_LAYOUT_FIRST :=
GBA_LAYOUT_LAST :=
GBA_SANITIZED_LAYOUT_LDFLAGS :=

ifeq ($(TARGET_OS),Linux)

TARGET_EXE_SUFFIX :=
TARGET_CFLAGS :=
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
# Non-PIE, the image at 0x40000000, clear of every region on both bit widths
# (docs/ARCHITECTURE.md, "The memory model").
GBA_LAYOUT_LDFLAGS := -no-pie -Wl,-Ttext-segment=0x40000000
GBA_SANITIZED_LAYOUT_LDFLAGS := -Wl,--defsym,gba_writable_data_floor=0x43000000
CONSOLE_LDFLAGS :=
GUI_LDFLAGS :=
REGIONS_LD_NAME := gba_regions.ld
GAME_LD_NAME := gba_game.ld
target_resources =

else ifeq ($(TARGET_OS),Darwin)

ifneq ($(BITS),64)
$(error TARGET_OS=Darwin is 64-bit only (BITS=64), not '$(BITS)': macOS runs no 32-bit executable since 10.15)
endif

# Apple's clang, the Mac's compiler; GNU as for the assembled data, whose text
# is GNU as's dialect, which clang's assembler refuses: Homebrew's
# x86_64-elf-binutils assembles it for ELF as on Linux, and
# build_glue/elf_to_flat.py converts the objects for clang to assemble as
# Mach-O (docs/DECISIONS.md, "The data for Windows and macOS is assembled on
# Linux and converted"; on a Mac the same steps run on the Mac).
TARGET_CC ?= clang
TARGET_AS ?= x86_64-elf-as
# The build glue's self-tests assemble their ELF fixtures with this assembler
# (build_glue/check_support.py, gnu_as): the Mac's own `as` writes Mach-O.
export GNU_AS := $(TARGET_AS)
# Upstream's hydra runs `$(OBJCOPY) -O binary` on its copy of the test
# executable on macOS (tools/mgba-rom-test-hydra/main.c, __APPLE__), which
# would turn the Mach-O file into a flat one: `true` does nothing instead.
OBJCOPY ?= true

TARGET_EXE_SUFFIX :=
TARGET_CFLAGS :=
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
CONSOLE_LDFLAGS :=
GUI_LDFLAGS :=
target_resources =

# The fixed-address image probe M0 found ld64 builds and the kernel loads
# with no slide (research/probes/README.md): no PIE, a one-page __PAGEZERO
# instead of the 4 GiB one (so that the GBA's addresses are free), the image
# at 0x40000000 as on Linux, and no chained fixups, without which ld64
# ignores the image base. ld64 warns that -no_pie and a small __PAGEZERO are
# deprecated for new macOS versions; for x86_64 they still work (probe M0:
# 100 of 100 starts on macos-15-intel).
MACHO_IMAGE_LDFLAGS := -Wl,-no_pie -Wl,-pagezero_size,0x1000 -Wl,-image_base,0x40000000 -Wl,-no_fixup_chains
# Each region's segment at its GBA address (gba_regions.macho.s; GBATEK "GBA
# Memory Map"), and the battle runner's stack at 0x01800000, as on Linux.
MACHO_SEGMENT_LDFLAGS := -Wl,-segaddr,__GBA_CALLSTK,0x01800000 -Wl,-segaddr,__GBA_EWRAM,0x02000000 \
                         -Wl,-segaddr,__GBA_IWRAM,0x03000000 -Wl,-segaddr,__GBA_IO,0x04000000 \
                         -Wl,-segaddr,__GBA_DEBUG,0x04FFF000 -Wl,-segaddr,__GBA_PALETTE,0x05000000 \
                         -Wl,-segaddr,__GBA_VRAM,0x06000000 -Wl,-segaddr,__GBA_OAM,0x07000000 \
                         -Wl,-segaddr,__GBA_SRAM,0x0E000000
# The weak references that only the game, or only the test runner, defines
# (platform/crt0.c): a definition elsewhere wins, and without one ld64 would
# refuse the link where GNU ld leaves them null; -U leaves them null too.
MACHO_WEAK_LDFLAGS := -Wl,-U,_gba_game_objects_linked -Wl,-U,_gPlatformTestEntrySize \
                      -Wl,-U,_PlatformTestTableBadEntry
GBA_LAYOUT_LDFLAGS := $(MACHO_IMAGE_LDFLAGS) $(MACHO_SEGMENT_LDFLAGS) $(MACHO_WEAK_LDFLAGS)
# __DATA follows __TEXT, which in the sanitized builds passes 0x42000000 (its
# read-only data); check_macho_layout.py refuses writable data there.
GBA_SANITIZED_LAYOUT_LDFLAGS := -Wl,-segaddr,__DATA,0x43000000
GBA_LINKER_SCRIPT_FLAG := -Wl,-alias_list,
REGIONS_LD_NAME := gba_regions.macho.aliases
GAME_LD_NAME := gba_game.macho.aliases
GBA_LAYOUT_FIRST := gba_regions.macho.s gba_game.macho.s
GBA_LAYOUT_LAST := gba_regions_end.macho.s

else ifeq ($(TARGET_OS),windows)

ifneq ($(BITS),64)
$(error TARGET_OS=windows is 64-bit only (BITS=64), not '$(BITS)': an i686 COFF object prefixes every C symbol with an underscore, which the linker scripts and the assembled data would need a second spelling)
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

# The layout the Windows loader accepted (loader probe W1):
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
