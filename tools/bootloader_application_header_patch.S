	.section .text, "ax"
	.align 2
	.globl ValidateCompactApplicationEndMarker
	.type ValidateCompactApplicationEndMarker, @function

/* Replacement for the stock validator at 0x00012AF0. */
ValidateCompactApplicationEndMarker:
	lis     4, 0x0008
	lwz     5, 0x0014(4)
	cmplwi  5, 0x001A
	blt     InvalidCompactApplication
	lis     6, 0x0028
	cmplw   5, 6
	bgt     InvalidCompactApplication
	andi.   6, 5, 1
	bne     InvalidCompactApplication
	add     4, 4, 5
	lhz     11, -2(4)
	cmplwi  11, 0x55AA
	bne     InvalidCompactApplication
	li      3, 1
	blr

InvalidCompactApplication:
	li      3, 0
	blr
	nop

	.size ValidateCompactApplicationEndMarker, .-ValidateCompactApplicationEndMarker
