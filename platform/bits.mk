# platform/bits.mk: BITS, the executable's width (32 or 64), when the command
# line gives none. Included by platform/Makefile, platform/m4a/Makefile and
# Makefile.native in place of a default of their own, before anything reads
# BITS. The default is 64 where the executable is for macOS, which runs no
# 32-bit program (target.mk refuses BITS=32 there): TARGET_OS if it is given,
# else the machine building (HOST_OS, `uname -s` unless given, as host.mk
# names it). Elsewhere it is 32, as before; the Windows cross build, whose
# commands give BITS=64, keeps target.mk's refusal of 32.
ifeq ($(or $(TARGET_OS),$(HOST_OS),$(shell uname -s)),Darwin)
BITS ?= 64
endif
BITS ?= 32
