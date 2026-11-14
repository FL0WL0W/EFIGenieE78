	.section .startup, "ax"
	.align 2
	.globl _start
	.type _start, @function
_start:
	wrteei	0

# Initialize every GPR before any value can be spilled in lock-step mode.

# 1 is deliberately preserved.  The resident bootloader constructed a
# fully initialized cache-as-RAM stack at 0x60000000 and calls this entry as
# a normal function.  Cold internal SRAM does not yet have valid ECC.
	li	0, 0
	li	2, 0
	li	3, 0
	li	4, 0
	li	5, 0
	li	6, 0
	li	7, 0
	li	8, 0
	li	9, 0
	li	10, 0
	li	11, 0
	li	12, 0
	li	13, 0
	li	14, 0
	li	15, 0
	li	16, 0
	li	17, 0
	li	18, 0
	li	19, 0
	li	20, 0
	li	21, 0
	li	22, 0
	li	23, 0
	li	24, 0
	li	25, 0
	li	26, 0
	li	27, 0
	li	28, 0
	li	29, 0
	li	30, 0
	li	31, 0

# Enable SPE before any compiled application code executes.
    mfmsr 3
    oris 3, 3, 0x0200
    mtmsr 3
    isync
    mtspr 512, 0

# Establish valid ECC across the complete application-side SRAM window.
# This includes 0x40010000, where the RAM-download kernel is linked, so an
# upload through UDS can write it without first entering BAM/boot-pin mode.
# The resident bootloader's live 0x40000000..0x40007FFF window is preserved.
	lis	5, __APPLICATION_SRAM_START@h
	ori	5, 5, __APPLICATION_SRAM_START@l
	lis	6, __APPLICATION_SRAM_END@h
	ori	6, 6, __APPLICATION_SRAM_END@l
	bl	InitializeSramEccRange
	b	ApplicationSramEccInitialized

# MPC5566 cold SRAM requires aligned 64-bit writes before smaller accesses.
# stmw 30 writes the zeroed 30/31 pair as one aligned 64-bit operation.
InitializeSramEccRange:
	subf	9, 5, 6
	srwi	9, 9, 3
	cmpwi	9, 0
	beqlr
	mtctr	9
InitializeApplicationSramEccLoop:
	stmw	30, 0(5)
	addi	5, 5, 8
	bdnz	InitializeApplicationSramEccLoop
	blr
ApplicationSramEccInitialized:

# SRAM now has valid ECC, so move off the bootloader's 16 KiB cache stack
# and onto the application's dedicated SRAM stack before calling any C code.
	lis	1, __SP_INIT@h
	ori	1, 1, __SP_INIT@l

# Copy ordinary initialized data from its flash load address to SRAM.
	lis	4, __DATA_LOAD@h
	ori	4, 4, __DATA_LOAD@l
	lis	5, __DATA_START@h
	ori	5, 5, __DATA_START@l
	lis	9, __DATA_SIZE@h
	ori	9, 9, __DATA_SIZE@l
	bl	CopyInitializedBytes

# Clear BSS.
	lis	5, __BSS_START@h
	ori	5, 5, __BSS_START@l
	lis	9, __BSS_SIZE@h
	ori	9, 9, __BSS_SIZE@l
	cmpwi	9, 0
	beq	BssInitialized
	mtctr	9
	subi	5, 5, 1
	li	4, 0
ClearBssLoop:
	stbu	4, 1(5)
	bdnz	ClearBssLoop
BssInitialized:

	# Replace all cache state inherited from the resident bootloader with a
	# known 8-way application configuration, then lock the complete interrupt
	# vector and shared-entry region before any application code runs.
	bl	InitializeAndLockInterruptCache

	# Run constructors after memory, the stack, and the cache are ready.
	bl	RunGlobalConstructors

	# Install the e200z6 IVORs and select MPC5566 INTC hardware-vector mode.
	# This also clears all inherited INTC priorities and leaves MSR[EE] clear.
	bl	InitializeHardwareInterrupts

	bl	main
ApplicationReturned:
	b	ApplicationReturned
	.size _start, .-_start

CopyInitializedBytes:
	cmpwi	9, 0
	beqlr
	mtctr	9
	subi	4, 4, 1
	subi	5, 5, 1
CopyInitializedBytesLoop:
	lbzu	6, 1(4)
	stbu	6, 1(5)
	bdnz	CopyInitializedBytesLoop
	blr

RunGlobalConstructors:
	stwu	1, -16(1)
	mflr	0
	stw	0, 20(1)

	lis	3, __preinit_array_start@h
	ori	3, 3, __preinit_array_start@l
	lis	4, __preinit_array_end@h
	ori	4, 4, __preinit_array_end@l
	bl	CallFunctionArray

	lis	3, __init_array_start@h
	ori	3, 3, __init_array_start@l
	lis	4, __init_array_end@h
	ori	4, 4, __init_array_end@l
	bl	CallFunctionArray

	lwz	0, 20(1)
	mtlr	0
	addi	1, 1, 16
	blr

# The MPC5566 has a unified 32-KiB cache with 32-byte lines. Discard the cache
# configuration and cache-as-RAM locks inherited from the bootloader, select
# the normal 128-set x 8-way organization, then touch-and-lock every line in
# the linker's contiguous vector-table and shared-entry range.
#
# Returns 3 = 1 when every lock completed without CUL/CLO, otherwise zero.
InitializeAndLockInterruptCache:
	# Disable cache lookups and clear all configurable fields. L1CSR0 changes
	# require the ordering sequence specified by the e200z6 documentation.
	li	3, 0
	msync
	isync
	mtspr	1010, 3
	isync

	# Remove every inherited line lock. CLFC clears itself when the operation
	# completes (approximately 134 clocks).
	li	3, 0x0100		# L1CSR0[CLFC]
	msync
	isync
	mtspr	1010, 3
ClearCacheLocksWait:
	mfspr	4, 1010
	andi.	4, 4, 0x0100
	bne	ClearCacheLocksWait

	# Invalidate all inherited cache contents after their lock bits are clear.
	li	3, 0x0002		# L1CSR0[CINV]
	msync
	isync
	mtspr	1010, 3
InvalidateCacheWait:
	mfspr	4, 1010
	andi.	4, 4, 0x0002
	bne	InvalidateCacheWait

	# CE=1 with every other field clear selects normal write-through operation,
	# enabled buffering/streaming, WAM=0, and 128 sets x 8 ways (CORG=0).
	li	3, 0x0001
	msync
	isync
	mtspr	1010, 3
	isync

	lis	4, __INTERRUPT_CACHE_LOCK_START@h
	ori	4, 4, __INTERRUPT_CACHE_LOCK_START@l
	lis	5, __INTERRUPT_CACHE_LOCK_END@h
	ori	5, 5, __INTERRUPT_CACHE_LOCK_END@l
LockInterruptCacheLine:
	cmplw	4, 5
	bge	InterruptCacheLockComplete
	icbtls	0, 0, 4
	addi	4, 4, 32
	b	LockInterruptCacheLine

InterruptCacheLockComplete:
	msync
	isync
	mfspr	3, 1010
	andi.	3, 3, 0x0600	# CUL or CLO indicates lock failure
	li	3, 0
	bnelr
	li	3, 1
	blr

# Call each non-null function pointer in the half-open range [3, 4).
CallFunctionArray:
	stwu	1, -32(1)
	mflr	0
	stw	0, 36(1)
	stw	30, 16(1)
	stw	31, 20(1)
	mr	30, 3
	mr	31, 4
CallFunctionArrayLoop:
	cmplw	30, 31
	beq	CallFunctionArrayComplete
	lwz	12, 0(30)
	addi	30, 30, 4
	cmpwi	12, 0
	beq	CallFunctionArrayLoop
	mtctr	12
	bctrl
	b	CallFunctionArrayLoop
CallFunctionArrayComplete:
	lwz	30, 16(1)
	lwz	31, 20(1)
	lwz	0, 36(1)
	mtlr	0
	addi	1, 1, 32
	blr

# Application callbacks exported to the patched resident bootloader through
# the compact header at 0x00080004. These routines can be called before _start,
# so they must not depend on the application's 2/13, data, BSS, or heap.
	.section .boot_callback_code, "ax"
	.align 2

E78LoadBootParameterBlock:
	stwu	1, -32(1)
	mflr	0
	stw	0, 36(1)
	stw	30, 24(1)
	stw	31, 28(1)

# Match the stock application's preparation of 0x40000480..0x400008BF.
	lis	3, 0x4000
	ori	3, 3, 0x0480
	li	30, 0
	li	31, 0
	li	5, 0x0088
	mtctr	5
E78ClearBootParameterWorkspace:
	stmw	30, 0(3)
	addi	3, 3, 8
	bdnz	E78ClearBootParameterWorkspace

# LoadBootParameterBlock is resident in the preserved stock bootloader.
	lis	12, 0x0000
	ori	12, 12, 0x8B28
	lis	3, 0x4000
	ori	3, 3, 0x0480
	mtctr	12
	bctrl

	lwz	30, 24(1)
	lwz	31, 28(1)
	lwz	0, 36(1)
	mtlr	0
	addi	1, 1, 32
	blr

	.globl E78BootValidateApplicationState
	.type E78BootValidateApplicationState, @function
E78BootValidateApplicationState:
# An application-requested upload is carried across a full software-system
# reset in the last aligned ECC granule of SRAM.  Never inspect that SRAM on
# a cold or externally generated reset: its ECC may not have been initialized.
	lis	4, 0xC3F9
	lwz	5, 0x000C(4)		# SIU.RSR
	lis	6, 0x0002		# RSR[SSRS]
	and.	5, 5, 6
	beq	E78BootPerformNormalValidation

	lis	4, __BOOT_UPLOAD_REQUEST@h
	ori	4, 4, __BOOT_UPLOAD_REQUEST@l
	lwz	5, 0(4)
	lis	6, 0x4555		# "EUPL"
	ori	6, 6, 0x504C
	cmpw	5, 6
	bne	E78BootPerformNormalValidation
	lwz	5, 4(4)
	lis	6, 0xBAAA		# complement of "EUPL"
	ori	6, 6, 0xAFB3
	cmpw	5, 6
	bne	E78BootPerformNormalValidation

# Consume the one-shot request before entering the upload routine. The ECC
# granule is already valid after the software reset, so ordinary word stores
# are safe here.
	li	5, 0
	stw	5, 0(4)
	stw	5, 4(4)
	msync

# The reset restarted BAM/the resident bootloader, so its cache-as-RAM stack
# has been constructed again and is safe to use. Enter the stock secondary
# upload routine without returning through the application-validation caller.
	wrteei	0
	lis	1, 0x6000
	ori	1, 1, 0x3FF0
	lis	12, 0x0002
	lwz	12, -0x5060(12)	# pointer at 0x0001AFA0
	mtctr	12
	bctr

E78BootPerformNormalValidation:
	stwu	1, -16(1)
	mflr	0
	stw	0, 20(1)
	bl	E78LoadBootParameterBlock

	lis	4, 0x4000
	lhz	5, 0x0824(4)
	li	3, 0
	cmpwi	5, 0
	beq	E78BootValidationComplete
	cmplwi	5, 0xFFFF
	beq	E78BootValidationComplete
	lhz	6, 0x081C(4)
	not	6, 6
	clrlwi	6, 6, 16
	cmpw	5, 6
	beq	E78BootValidationComplete
	li	3, 1
E78BootValidationComplete:
	lwz	0, 20(1)
	mtlr	0
	addi	1, 1, 16
	blr
	.size E78BootValidateApplicationState, .-E78BootValidateApplicationState

	.globl E78BootGetParameterBlock758
	.type E78BootGetParameterBlock758, @function
E78BootGetParameterBlock758:
	stwu	1, -16(1)
	mflr	0
	stw	0, 20(1)
	bl	E78LoadBootParameterBlock
	lis	3, 0x4000
	ori	3, 3, 0x0758
	lwz	0, 20(1)
	mtlr	0
	addi	1, 1, 16
	blr
	.size E78BootGetParameterBlock758, .-E78BootGetParameterBlock758

	.globl E78BootGetApplicationIdentifier
	.type E78BootGetApplicationIdentifier, @function
E78BootGetApplicationIdentifier:
	lis	3, E78ApplicationIdentifier@h
	ori	3, 3, E78ApplicationIdentifier@l
	blr
	.size E78BootGetApplicationIdentifier, .-E78BootGetApplicationIdentifier

	.globl E78BootGetParameterBlock754
	.type E78BootGetParameterBlock754, @function
E78BootGetParameterBlock754:
	stwu	1, -16(1)
	mflr	0
	stw	0, 20(1)
	bl	E78LoadBootParameterBlock
	lis	3, 0x4000
	ori	3, 3, 0x0754
	lwz	0, 20(1)
	mtlr	0
	addi	1, 1, 16
	blr
	.size E78BootGetParameterBlock754, .-E78BootGetParameterBlock754

	.align 2
E78ApplicationIdentifier:
# The four bytes returned by the stock E78 application for boot service 1A C1.
	.byte 0x00, 0xC1, 0x52, 0x45

# Request a full software-system reset. The bootloader will recreate all of
# its own context and E78BootValidateApplicationState will consume the retained
# SRAM marker and enter its secondary upload routine.
	.section .text_booke, "ax"
	.align 2
	.globl ExitToBootloaderUploadRoutine
	.type ExitToBootloaderUploadRoutine, @function
ExitToBootloaderUploadRoutine:
	wrteei	0
# Write the magic and its complement into one aligned 64-bit ECC granule.
# 30/31 are intentionally not preserved because this function cannot return.
	lis	29, __BOOT_UPLOAD_REQUEST@h
	ori	29, 29, __BOOT_UPLOAD_REQUEST@l
	lis	30, 0x4555		# "EUPL"
	ori	30, 30, 0x504C
	lis	31, 0xBAAA
	ori	31, 31, 0xAFB3
	stmw	30, 0(29)
	msync

# SIU.SRCR[SSR] requests a full internal system reset through BAM. SER is not
# sufficient because it would not reconstruct the resident bootloader state.
	lis	3, 0xC3F9
	lis	4, 0x8000
	stw	4, 0x0010(3)		# SIU.SRCR = SSR
	msync
E78WaitForSoftwareSystemReset:
	b	E78WaitForSoftwareSystemReset
	.size ExitToBootloaderUploadRoutine, .-ExitToBootloaderUploadRoutine
