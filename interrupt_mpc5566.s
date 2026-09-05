
/*
 * Hardware-vectored interrupt frame.
 *
 * Offset zero contains the interrupted r0, stored by the vector slot's stwu.
 * Offset four is intentionally reserved: a normal EABI C++ callee may use the
 * caller-provided LR save word at 4(r1).  The remainder is private to this
 * assembly trampoline.  The 80-byte size preserves the stack alignment used
 * by the current PowerPC EABI toolchain.
 */
.set SPEFSCR,              512

.set ORIGINAL_R0_OFFSET,     0
.set ABI_LR_SAVE_OFFSET,     4
.set R3_OFFSET,              8
.set R4_OFFSET,             12
.set R5_OFFSET,             16
.set R6_OFFSET,             20
.set R7_OFFSET,             24
.set R8_OFFSET,             28
.set R9_OFFSET,             32
.set R10_OFFSET,            36
.set R11_OFFSET,            40
.set R12_OFFSET,            44
.set LR_OFFSET,             48
.set CTR_OFFSET,            52
.set CR_OFFSET,             56
.set XER_OFFSET,            60
.set SPEFSCR_OFFSET,        64
.set SRR0_OFFSET,           68
.set SRR1_OFFSET,           72
.set RETURN_KIND_OFFSET,    76
.set INTERRUPT_FRAME_SIZE,  80

.if (INTERRUPT_FRAME_SIZE & 15)
    .error "INTERRUPT_FRAME_SIZE must preserve 16-byte stack alignment"
.endif
.if ((RETURN_KIND_OFFSET + 4) > INTERRUPT_FRAME_SIZE)
    .error "interrupt context does not fit in INTERRUPT_FRAME_SIZE"
.endif

.section .text_booke, "ax"
.align 2

.macro set_ivor spr, vector
    li      r5, \vector@l
    mtspr   \spr, r5
.endm

/*
 * Select the MPC5566 INTC hardware-vector mode.
 *
 * This routine deliberately leaves MSR[EE] cleared.  Callers must configure
 * each peripheral interrupt source, assign it a nonzero INTC_PSR priority,
 * clear any pending peripheral flags, and then execute wrteei 1 when the
 * system is ready to accept external interrupts.
 *
 * Any interrupt configuration inherited from the bootloader is discarded:
 * CPR is first raised to 15, every implemented PSR is cleared to priority 0,
 * and all eight software-settable requests are cleared. INTC_MCR is written
 * rather than read-modify-written because all fields other than HVEN must be
 * zero for this 16-byte hardware vector arrangement. CPR is finally lowered
 * to priority 0; sources remain disabled until the application explicitly
 * gives their PSR a priority in the range 1-15.
 */
.global InitializeHardwareInterrupts
.type InitializeHardwareInterrupts, @function
InitializeHardwareInterrupts:
    wrteei  0

    lis     r3, 0xFFF4
    ori     r3, r3, 0x8000          /* INTC base: 0xFFF48000 */
    li      r4, 15
    stw     r4, 0x08(r3)            /* Mask every INTC priority */
    mbar

    li      r4, 1                   /* SSCIR CLR bit */
    addi    r5, r3, 0x20            /* INTC_SSCIR0 */
    li      r6, 8
1:
    stb     r4, 0(r5)
    addi    r5, r5, 1
    addic.  r6, r6, -1
    bne     1b

    li      r4, 0
    addi    r5, r3, 0x40            /* INTC_PSR0 */
    li      r6, 330                 /* INTC_PSR0 through INTC_PSR329 */
2:
    stb     r4, 0(r5)
    addi    r5, r5, 1
    addic.  r6, r6, -1
    bne     2b

    lis     r5, MPC5566_INTC_VectorTable@h
    ori     r5, r5, MPC5566_INTC_VectorTable@l
    mtspr   63, r5                  /* IVPR */
    isync

    mfspr   r4, 1008                /* HID0 */
    ori     r4, r4, 0x0100          /* DAPUEN: debug uses DSRR0/1 + rfdi */
    mtspr   1008, r4
    isync

    set_ivor 400, CoreCriticalInputVector
    set_ivor 401, CoreMachineCheckVector
    set_ivor 402, CoreDataStorageVector
    set_ivor 403, CoreInstructionStorageVector
    set_ivor 404, CoreExternalInputVector
    set_ivor 405, CoreAlignmentVector
    set_ivor 406, CoreProgramVector
    set_ivor 407, CoreFloatingPointUnavailableVector
    set_ivor 408, CoreSystemCallVector
    set_ivor 409, CoreAuxiliaryProcessorUnavailableVector
    set_ivor 410, CoreDecrementerVector
    set_ivor 411, CoreFixedIntervalTimerVector
    set_ivor 412, CoreWatchdogTimerVector
    set_ivor 413, CoreDataTLBErrorVector
    set_ivor 414, CoreInstructionTLBErrorVector
    set_ivor 415, CoreDebugVector
    set_ivor 528, CoreSPEUnavailableVector
    set_ivor 529, CoreSPEDataExceptionVector
    set_ivor 530, CoreSPERoundExceptionVector
    isync

    li      r4, 1
    stw     r4, 0x00(r3)            /* INTC_MCR: HVEN=1, VTES=0 */
    li      r4, 0
    stw     r4, 0x08(r3)            /* INTC_CPR: PRI=0 */
    mbar
    isync
    blr
.size InitializeHardwareInterrupts, .-InitializeHardwareInterrupts

/*
 * Every weak handler below has the normal EABI signature:
 *
 *     void HandlerName(void);
 *
 * A handler implemented in C++ must use extern "C" to suppress name mangling.
 * The trampolines preserve the interrupted scalar C/C++ ABI context and
 * provide the stack alignment and caller LR save word required by a normal
 * compiled function before invoking it through CTR with bctrl.
 */
.type Default_Handler, @function
Default_Handler:
    b  Default_Handler
.size Default_Handler, .-Default_Handler

.macro set_weak_default name
    .weak \name
    .type \name, @function
    .set \name, Default_Handler
.endm

CommonInterruptEntry:
    stw     r3, R3_OFFSET(r1)
    mfsrr0  r3
    stw     r3, SRR0_OFFSET(r1)
    mfsrr1  r3
    stw     r3, SRR1_OFFSET(r1)

    wrteei  1

    stw     r4, R4_OFFSET(r1)
    stw     r5, R5_OFFSET(r1)
    stw     r6, R6_OFFSET(r1)
    stw     r7, R7_OFFSET(r1)
    stw     r8, R8_OFFSET(r1)
    stw     r9, R9_OFFSET(r1)
    stw     r10,R10_OFFSET(r1)
    stw     r11,R11_OFFSET(r1)
    stw     r12,R12_OFFSET(r1)

    mflr    r3
    stw     r3, LR_OFFSET(r1)
    mfctr   r3
    stw     r3, CTR_OFFSET(r1)
    mfcr    r3
    stw     r3, CR_OFFSET(r1)
    mfxer   r3
    stw     r3, XER_OFFSET(r1)
    mfspr   r3, SPEFSCR
    stw     r3, SPEFSCR_OFFSET(r1)

    mtctr   r0
    bctrl

    mbar
    
    lwz     r4, R4_OFFSET(r1)
    lwz     r5, R5_OFFSET(r1)
    lwz     r6, R6_OFFSET(r1)
    lwz     r7, R7_OFFSET(r1)
    lwz     r8, R8_OFFSET(r1)
    lwz     r9, R9_OFFSET(r1)
    lwz     r10,R10_OFFSET(r1)
    lwz     r11,R11_OFFSET(r1)
    lwz     r12,R12_OFFSET(r1)
    lwz     r3, LR_OFFSET(r1)
    mtlr    r3
    lwz     r3, CTR_OFFSET(r1)
    mtctr   r3
    lwz     r3, CR_OFFSET(r1)
    mtcr    r3
    lwz     r3, XER_OFFSET(r1)
    mtxer   r3
    lwz     r3, SPEFSCR_OFFSET(r1)
    mtspr   SPEFSCR, r3

    wrteei  0
    
	lis   r3,0xFFF4
	ori   r3,r3,0x8018
	li    r0,0
	stw   r0,0(r3)       /* INTC.EOIR = 0 */
    lwz     r3, SRR0_OFFSET(r1)
    mtsrr0  r3
    lwz     r3, SRR1_OFFSET(r1)
    mtsrr1  r3
    lwz     r3, R3_OFFSET(r1)
    lwz     r0, 0(r1)
    addi    r1, r1, INTERRUPT_FRAME_SIZE
    rfi

/*
 * Core exceptions do not participate in the INTC acknowledge/EOI protocol.
 * The first-stage entries select the save/restore register pair used by the
 * exception class, then share the normal C/C++ volatile-context body.
 *
 * Return kind 0: SRR0/SRR1 and rfi
 * Return kind 1: CSRR0/CSRR1 and rfci
 * Return kind 2: DSRR0/DSRR1 and rfdi
 */
CommonCoreExceptionEntry:
    stw     r3, R3_OFFSET(r1)
    mfspr   r3, 26                  /* SRR0 */
    stw     r3, SRR0_OFFSET(r1)
    mfspr   r3, 27                  /* SRR1 */
    stw     r3, SRR1_OFFSET(r1)
    li      r3, 0
    b       CommonCoreExceptionBody

CommonCriticalExceptionEntry:
    stw     r3, R3_OFFSET(r1)
    mfspr   r3, 58                  /* CSRR0 */
    stw     r3, SRR0_OFFSET(r1)
    mfspr   r3, 59                  /* CSRR1 */
    stw     r3, SRR1_OFFSET(r1)
    li      r3, 1
    b       CommonCoreExceptionBody

CommonDebugExceptionEntry:
    stw     r3, R3_OFFSET(r1)
    mfspr   r3, 574                 /* DSRR0 */
    stw     r3, SRR0_OFFSET(r1)
    mfspr   r3, 575                 /* DSRR1 */
    stw     r3, SRR1_OFFSET(r1)
    li      r3, 2

CommonCoreExceptionBody:
    stw     r3, RETURN_KIND_OFFSET(r1)
    stw     r4, R4_OFFSET(r1)
    stw     r5, R5_OFFSET(r1)
    stw     r6, R6_OFFSET(r1)
    stw     r7, R7_OFFSET(r1)
    stw     r8, R8_OFFSET(r1)
    stw     r9, R9_OFFSET(r1)
    stw     r10,R10_OFFSET(r1)
    stw     r11,R11_OFFSET(r1)
    stw     r12,R12_OFFSET(r1)

    mflr    r3
    stw     r3, LR_OFFSET(r1)
    mfctr   r3
    stw     r3, CTR_OFFSET(r1)
    mfcr    r3
    stw     r3, CR_OFFSET(r1)
    mfxer   r3
    stw     r3, XER_OFFSET(r1)
    mfspr   r3, SPEFSCR
    stw     r3, SPEFSCR_OFFSET(r1)

    mtctr   r0
    bctrl

    mbar
    wrteei  0

    lwz     r4, R4_OFFSET(r1)
    lwz     r5, R5_OFFSET(r1)
    lwz     r6, R6_OFFSET(r1)
    lwz     r7, R7_OFFSET(r1)
    lwz     r8, R8_OFFSET(r1)
    lwz     r9, R9_OFFSET(r1)
    lwz     r10,R10_OFFSET(r1)
    lwz     r11,R11_OFFSET(r1)
    lwz     r12,R12_OFFSET(r1)
    lwz     r3, LR_OFFSET(r1)
    mtlr    r3
    lwz     r3, CTR_OFFSET(r1)
    mtctr   r3
    lwz     r3, XER_OFFSET(r1)
    mtxer   r3
    lwz     r3, SPEFSCR_OFFSET(r1)
    mtspr   SPEFSCR, r3

    lwz     r3, RETURN_KIND_OFFSET(r1)
    cmpwi   r3, 1
    beq     .LCoreReturnCritical
    cmpwi   r3, 2
    beq     .LCoreReturnDebug

.LCoreReturnNormal:
    lwz     r3, CR_OFFSET(r1)
    mtcr    r3
    lwz     r3, SRR0_OFFSET(r1)
    mtspr   26, r3                  /* SRR0 */
    lwz     r3, SRR1_OFFSET(r1)
    mtspr   27, r3                  /* SRR1 */
    lwz     r3, R3_OFFSET(r1)
    lwz     r0, ORIGINAL_R0_OFFSET(r1)
    addi    r1, r1, INTERRUPT_FRAME_SIZE
    rfi

.LCoreReturnCritical:
    lwz     r3, CR_OFFSET(r1)
    mtcr    r3
    lwz     r3, SRR0_OFFSET(r1)
    mtspr   58, r3                  /* CSRR0 */
    lwz     r3, SRR1_OFFSET(r1)
    mtspr   59, r3                  /* CSRR1 */
    lwz     r3, R3_OFFSET(r1)
    lwz     r0, ORIGINAL_R0_OFFSET(r1)
    addi    r1, r1, INTERRUPT_FRAME_SIZE
    rfci

.LCoreReturnDebug:
    lwz     r3, CR_OFFSET(r1)
    mtcr    r3
    lwz     r3, SRR0_OFFSET(r1)
    mtspr   574, r3                 /* DSRR0 */
    lwz     r3, SRR1_OFFSET(r1)
    mtspr   575, r3                 /* DSRR1 */
    lwz     r3, R3_OFFSET(r1)
    lwz     r0, ORIGINAL_R0_OFFSET(r1)
    addi    r1, r1, INTERRUPT_FRAME_SIZE
    rfdi

.macro core_exception_wrapper vector, body, entry=CommonCoreExceptionEntry
    set_weak_default \body
    .balign 0x10
    .global \vector
\vector:
    stwu    r0,-INTERRUPT_FRAME_SIZE(r1)
    lis     r0,\body@h
    ori     r0,r0,\body@l
    b       \entry
.endm


.macro intc_wrapper body
    set_weak_default \body
    .balign 0x10
    stwu    r0,-INTERRUPT_FRAME_SIZE(r1)
    lis     r0,\body@h
    ori     r0,r0,\body@l
    b       CommonInterruptEntry
.endm

.macro intc_default
    .balign 0x10
    b Default_Handler
    nop
    nop
    nop
.endm

.macro dspi_vectors module
    intc_wrapper DSPI_\module\()_Overrun_Handler
    intc_wrapper DSPI_\module\()_EndOfQueue_Handler
    intc_wrapper DSPI_\module\()_TransmitFill_Handler
    intc_wrapper DSPI_\module\()_TransferComplete_Handler
    intc_wrapper DSPI_\module\()_ReceiveDrain_Handler
.endm

.macro flexcan_vector_start module
    intc_wrapper FlexCAN_\module\()_BusOffWarning_Handler
    intc_wrapper FlexCAN_\module\()_Error_Handler
    intc_default
.endm

.macro flexcan_vector_end module
    intc_wrapper FlexCAN_\module\()_Buffers16To31_Handler
    intc_wrapper FlexCAN_\module\()_Buffers32To63_Handler
.endm

/*
 * MPC5566 hardware vector table from reference-manual Table 10-9.
 * IVPR supplies the upper 16 address bits, so the table requires 64 KiB
 * alignment. Every expansion below emits exactly one 16-byte vector slot.
 */
.section .intc_vector_table, "ax"
.balign 0x10000
.global MPC5566_INTC_VectorTable
MPC5566_INTC_VectorTable:

/* 0-7: INTC software-settable interrupts. */
.irp index,0,1,2,3,4,5,6,7
    intc_wrapper INTC_Software\index\()_Handler
.endr

/* 8-9: watchdog and memory non-correctable errors. */
intc_wrapper ECSM_SoftwareWatchdog_Handler
intc_wrapper ECSM_NonCorrectableError_Handler

/* 10-42: eDMA low-channel error and channels 0-31. */
intc_wrapper EDMA_Channels0To31Error_Handler
.irp channel,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    intc_wrapper EDMA_Channel\channel\()_Handler
.endr

/* 43-50: FMPLL and SIU external interrupts. */
intc_wrapper FMPLL_LossOfClock_Handler
intc_wrapper FMPLL_LossOfLock_Handler
intc_wrapper SIU_ExternalInterruptOverrun_Handler
intc_wrapper SIU_ExternalInterrupt0_Handler
intc_wrapper SIU_ExternalInterrupt1_Handler
intc_wrapper SIU_ExternalInterrupt2_Handler
intc_wrapper SIU_ExternalInterrupt3_Handler
intc_wrapper SIU_ExternalInterrupts4To15_Handler

/* 51-66: eMIOS channels 0-15. */
.irp channel,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    intc_wrapper EMIOS_Channel\channel\()_Handler
.endr

/* 67-99: eTPU global exception and engine A channels 0-31. */
intc_wrapper ETPU_GlobalException_Handler
.irp channel,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    intc_wrapper ETPU_A_Channel\channel\()_Handler
.endr

/* 100-130: eQADC overrun and the five interrupt sources for FIFOs 0-5. */
intc_wrapper EQADC_Overrun_Handler
.irp fifo,0,1,2,3,4,5
    intc_wrapper EQADC_FIFO\fifo\()_NonCoherency_Handler
    intc_wrapper EQADC_FIFO\fifo\()_Pause_Handler
    intc_wrapper EQADC_FIFO\fifo\()_EndOfQueue_Handler
    intc_wrapper EQADC_FIFO\fifo\()_CommandFill_Handler
    intc_wrapper EQADC_FIFO\fifo\()_ResultDrain_Handler
.endr

/* 131-145: DSPI B, C and D. */
dspi_vectors B
dspi_vectors C
dspi_vectors D

/* 146-151: eSCI A/B and reserved sources. */
intc_wrapper ESCI_A_Handler
.rept 2
    intc_default
.endr
intc_wrapper ESCI_B_Handler
.rept 2
    intc_default
.endr

/* 152-193: FlexCAN A and C. Each module includes its reserved vector. */
flexcan_vector_start A
.irp buffer,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    intc_wrapper FlexCAN_A_Buffer\buffer\()_Handler
.endr
flexcan_vector_end A
flexcan_vector_start C
.irp buffer,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    intc_wrapper FlexCAN_C_Buffer\buffer\()_Handler
.endr
flexcan_vector_end C

/* 194-201: FEC followed by five reserved sources. */
intc_wrapper FEC_TransmitFrame_Handler
intc_wrapper FEC_ReceiveFrame_Handler
intc_wrapper FEC_CombinedError_Handler
.rept 5
    intc_default
.endr

/* 202-209: eMIOS channels 16-23. */
.irp channel,16,17,18,19,20,21,22,23
    intc_wrapper EMIOS_Channel\channel\()_Handler
.endr

/* 210-242: eDMA high-channel error and channels 32-63. */
intc_wrapper EDMA_Channels32To63Error_Handler
.irp channel,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
    intc_wrapper EDMA_Channel\channel\()_Handler
.endr

/* 243-274: eTPU engine B channels 0-31. */
.irp channel,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    intc_wrapper ETPU_B_Channel\channel\()_Handler
.endr

/* 275-279: DSPI A. */
dspi_vectors A

/* 280-300: FlexCAN B. */
flexcan_vector_start B
.irp buffer,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    intc_wrapper FlexCAN_B_Buffer\buffer\()_Handler
.endr
flexcan_vector_end B

/* 301-307: reserved. */
.rept 7
    intc_default
.endr

/* 308-328: FlexCAN D. */
flexcan_vector_start D
.irp buffer,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    intc_wrapper FlexCAN_D_Buffer\buffer\()_Handler
.endr
flexcan_vector_end D

/* 329-331: reserved. */
.rept 3
    intc_default
.endr

MPC5566_INTC_VectorTableEnd:

/*
 * e200z6 core exception vectors. These deliberately follow the INTC table so
 * they share its 64-KiB IVPR region without colliding with hardware-vector
 * INTC slots. InitializeHardwareInterrupts writes each label into its IVOR.
 *
 * IVOR4 is not used while INTC_MCR[HVEN] is set, but it is installed so the
 * core vector set remains complete if software-vector mode is selected later.
 */
core_exception_wrapper CoreCriticalInputVector, CriticalInput_Handler, CommonCriticalExceptionEntry
core_exception_wrapper CoreMachineCheckVector, MachineCheck_Handler, CommonCriticalExceptionEntry
core_exception_wrapper CoreDataStorageVector, DataStorage_Handler
core_exception_wrapper CoreInstructionStorageVector, InstructionStorage_Handler
core_exception_wrapper CoreExternalInputVector, ExternalInput_Handler
core_exception_wrapper CoreAlignmentVector, Alignment_Handler
core_exception_wrapper CoreProgramVector, Program_Handler
core_exception_wrapper CoreFloatingPointUnavailableVector, FloatingPointUnavailable_Handler
core_exception_wrapper CoreSystemCallVector, SystemCall_Handler
core_exception_wrapper CoreAuxiliaryProcessorUnavailableVector, AuxiliaryProcessorUnavailable_Handler
core_exception_wrapper CoreDecrementerVector, Decrementer_Handler
core_exception_wrapper CoreFixedIntervalTimerVector, FixedIntervalTimer_Handler
core_exception_wrapper CoreWatchdogTimerVector, WatchdogTimer_Handler, CommonCriticalExceptionEntry
core_exception_wrapper CoreDataTLBErrorVector, DataTLBError_Handler
core_exception_wrapper CoreInstructionTLBErrorVector, InstructionTLBError_Handler
core_exception_wrapper CoreDebugVector, Debug_Handler, CommonDebugExceptionEntry
core_exception_wrapper CoreSPEUnavailableVector, SPEUnavailable_Handler
core_exception_wrapper CoreSPEDataExceptionVector, SPEDataException_Handler
core_exception_wrapper CoreSPERoundExceptionVector, SPERoundException_Handler

MPC5566_CoreExceptionVectorTableEnd:
