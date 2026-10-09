# platform/host.mk: the operating system the native executable runs on, and
# what follows from it. Included by platform/Makefile, platform/m4a/Makefile
# and Makefile.native, so that the platform layer and the game agree.
#
# HOST_OS names it as `uname -s` does (it defaults to that). Each OS has one
# source for the host layer beside the one every POSIX system shares
# (platform/host_*.h are the interface; docs/ARCHITECTURE.md, "The host
# layer"); Linux is the only one so far, so a port adds its file and its
# value here. Named HOST_OS, not OS, because Windows sets OS in every
# environment and make would take it.
HOST_OS ?= $(shell uname -s)

NM ?= nm

ifeq ($(HOST_OS),Linux)

HOST_SRCS := host_posix.c host_linux.c

# $(call write_symbol_map,EXECUTABLE,MAP): the function map symbols.c reads
# to name the game's callbacks (symbols.h has the format), written after the
# link. nm's System V format names each symbol's type, so the map holds what
# the ELF symbol table calls a function with a size; -p keeps the table's
# order, which the stable sort by address keeps among aliases (the first is
# the name the replays have always shown). The addresses are nm's
# fixed-width lower-case hexadecimal, so a text sort is a numeric one.
write_symbol_map = $(NM) -p --defined-only --format=sysv $(1) > $(2).nm \
	&& awk -F'|' '{ for (i = 1; i <= 5; i++) gsub(/ /, "", $$i) } $$4 == "FUNC" && $$5 != "" && $$1 != "" { print $$2, $$1 }' $(2).nm \
	| LC_ALL=C sort -s -k1,1 > $(2).tmp \
	&& rm -f $(2).nm && mv $(2).tmp $(2)

else
$(error HOST_OS must be Linux (the only port so far), not '$(HOST_OS)')
endif
