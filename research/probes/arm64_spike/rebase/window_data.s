# window_data.s: 4-byte address words as the window model writes them: each
# target's offset in the window (its address minus WINDOW_BASE, 2^32), a
# relocation the linker resolves (R_X86_64_32 with the addend -2^32). The
# first byte makes the words unaligned, as in script bytecode.
	.section .rodata
	.globl gTestBlob
gTestBlob:
	.byte 0x23
	.4byte gTestString - 0x100000000
	.4byte TestNative + 0x02000000 - 0x100000000   # a native pointer with the script tag
	.4byte gTestEwramVar - 0x100000000
	.4byte 0                                       # a null pointer
	.globl gTestString
gTestString:
	.asciz "HELLO"
	.section .note.GNU-stack,"",@progbits
