	.section .text, "ax"
	.align 2
	.globl ValidateCompactApplicationEndMarker
	.type ValidateCompactApplicationEndMarker, @function

/* Replacement for the stock validator at 0x00012AF0. */
ValidateCompactApplicationEndMarker:
	lis     r4, 0x0008
	lwz     r5, 0x0014(r4)
	cmplwi  r5, 0x001A
	blt     InvalidCompactApplication
	lis     r6, 0x0028
	cmplw   r5, r6
	bgt     InvalidCompactApplication
	andi.   r6, r5, 1
	bne     InvalidCompactApplication
	add     r4, r4, r5
	lhz     r11, -2(r4)
	cmplwi  r11, 0x55AA
	bne     InvalidCompactApplication
	li      r3, 1
	blr

InvalidCompactApplication:
	li      r3, 0
	blr
	nop

	.size ValidateCompactApplicationEndMarker, .-ValidateCompactApplicationEndMarker
