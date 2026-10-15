/* delta32.s: a 4-byte data word holding a symbol's offset from the start of
   the GBA window instead of its absolute address (research/probes/arm64_spike,
   the window model). Assembled for arm64 Mach-O here to see the relocations
   clang emits; linking needs ld64 (a runner). */
	.section __DATA,__gbawin
	.globl _gba_window
_gba_window:
	.space 16
	.section __TEXT,__text
	.globl _native_func
_native_func:
	ret
	.section __DATA,__script_data
	.globl _script
_script:
	.byte 0x23
	.long _native_func - _gba_window + 0x02000000  /* a callnative word with the script tag */
	.long _other_script - _gba_window
	.long _extern_label + 12 - _gba_window          /* a symbol from another object */
_other_script:
	.byte 2
