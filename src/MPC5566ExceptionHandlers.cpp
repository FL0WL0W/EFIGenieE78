#include "ICommunicationService.h"
#include "MPC5xxx.h"
#include "MPC5xxxFlexCAN2Service.h"

#include <cstdint>
#include <cstddef>
#include <cstdio>

using EmbeddedIOServices::ICommunicationService;

extern ICommunicationService* isotpConsole;

namespace
{
	enum class SavedStateKind : std::uint8_t
	{
		Normal,
		Critical,
		Debug,
	};

	struct ExceptionSnapshot
	{
		std::uint32_t ProgramCounter;
		std::uint32_t MachineState;
		std::uint32_t DataExceptionAddress;
		std::uint32_t ExceptionSyndrome;
		std::uint32_t MachineCheckSyndrome;
	};

	// Matches the 80-byte context saved by CommonCoreExceptionBody.
	struct CoreExceptionFrame
	{
		std::uint32_t OriginalR0;
		std::uint32_t AbiLrSave;
		std::uint32_t Gpr3Through12[10];
		std::uint32_t Lr, Ctr, Cr, Xer, Spefscr, Srr0, Srr1, ReturnKind;
	};
	static_assert(sizeof(CoreExceptionFrame) == 80);
	static_assert(offsetof(CoreExceptionFrame, Gpr3Through12) == 8);
	static_assert(offsetof(CoreExceptionFrame, Lr) == 48);
	static_assert(offsetof(CoreExceptionFrame, Srr0) == 68);

	struct FaultRegisters
	{
		std::uint32_t R1, R3, R4, R28, R30, Lr, R28Plus8, R30Plus16;
	};

	std::uint32_t ReadSramWord(const std::uint32_t address)
	{
		// Never turn the fault report into another invalid-memory access.
		if ((address & 3U) != 0U || address < 0x40000000U || address > 0x4001FFFCU)
			return 0xFFFFFFFFU;
		return *reinterpret_cast<volatile const std::uint32_t*>(address);
	}

	std::uint32_t ReadSRR0()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 26" : "=r"(value));
		return value;
	}

	std::uint32_t ReadSRR1()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 27" : "=r"(value));
		return value;
	}

	std::uint32_t ReadCSRR0()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 58" : "=r"(value));
		return value;
	}

	std::uint32_t ReadCSRR1()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 59" : "=r"(value));
		return value;
	}

	std::uint32_t ReadDSRR0()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 574" : "=r"(value));
		return value;
	}

	std::uint32_t ReadDSRR1()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 575" : "=r"(value));
		return value;
	}

	std::uint32_t ReadDEAR()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 61" : "=r"(value));
		return value;
	}

	std::uint32_t ReadESR()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 62" : "=r"(value));
		return value;
	}

	std::uint32_t ReadMCSR()
	{
		std::uint32_t value;
		asm volatile("mfspr %0, 572" : "=r"(value));
		return value;
	}

	ExceptionSnapshot CaptureException(const SavedStateKind stateKind)
	{
		ExceptionSnapshot snapshot;
		switch (stateKind)
		{
		case SavedStateKind::Critical:
			snapshot.ProgramCounter = ReadCSRR0();
			snapshot.MachineState = ReadCSRR1();
			break;
		case SavedStateKind::Debug:
			snapshot.ProgramCounter = ReadDSRR0();
			snapshot.MachineState = ReadDSRR1();
			break;
		default:
			snapshot.ProgramCounter = ReadSRR0();
			snapshot.MachineState = ReadSRR1();
			break;
		}
		snapshot.DataExceptionAddress = ReadDEAR();
		snapshot.ExceptionSyndrome = ReadESR();
		snapshot.MachineCheckSyndrome = ReadMCSR();
		return snapshot;
	}

	void ServiceCoreWatchdog()
	{
		const std::uint32_t watchdogService = 0x40000000U;
		asm volatile("mtspr 336, %0" :: "r"(watchdogService) : "memory");
	}

	[[noreturn]] void FatalException(
		const char* const name,
		const SavedStateKind stateKind = SavedStateKind::Normal,
		const FaultRegisters* registers = nullptr)
	{
		// Core-exception entry has MSR[EE] clear. Keep it clear: PollFlexCAN()
		// advances both CAN and ISO-TP explicitly without allowing unrelated
		// application interrupts to run in a corrupted execution context.
		asm volatile("wrteei 0\n\tisync" ::: "memory");

		const ExceptionSnapshot snapshot = CaptureException(stateKind);
		static volatile bool handlingException = false;
		const bool nestedException = handlingException;
		handlingException = true;
		bool reportQueued = false;

		for (;;)
		{
			ServiceCoreWatchdog();
			MPC5xxx::MPC5xxxFlexCAN2Service::PollFlexCAN(CAN_A);

			if (!nestedException && !reportQueued && isotpConsole != nullptr &&
				isotpConsole->Ready())
			{
				std::printf(
					"FATAL %s PC=%08lX MSR=%08lX DEAR=%08lX ESR=%08lX MCSR=%08lX",
					name,
					static_cast<unsigned long>(snapshot.ProgramCounter),
					static_cast<unsigned long>(snapshot.MachineState),
					static_cast<unsigned long>(snapshot.DataExceptionAddress),
					static_cast<unsigned long>(snapshot.ExceptionSyndrome),
					static_cast<unsigned long>(snapshot.MachineCheckSyndrome));
				if (registers != nullptr)
				{
					std::printf(
						" R1=%08lX R3=%08lX R4=%08lX R28=%08lX R30=%08lX LR=%08lX R28_8=%08lX R30_16=%08lX",
						static_cast<unsigned long>(registers->R1),
						static_cast<unsigned long>(registers->R3),
						static_cast<unsigned long>(registers->R4),
						static_cast<unsigned long>(registers->R28),
						static_cast<unsigned long>(registers->R30),
						static_cast<unsigned long>(registers->Lr),
						static_cast<unsigned long>(registers->R28Plus8),
						static_cast<unsigned long>(registers->R30Plus16));
				}
				// One newline queues the complete report as one console message.
				std::printf("\n");
				reportQueued = true;
			}
		}
	}
}

extern "C" void CriticalInput_Handler()
{
	FatalException("CriticalInput", SavedStateKind::Critical);
}

extern "C" void MachineCheck_Handler()
{
	FatalException("MachineCheck", SavedStateKind::Critical);
}

extern "C" void DataStorage_Handler(
	const CoreExceptionFrame* frame, std::uint32_t interruptedR28, std::uint32_t interruptedR30)
{
	const FaultRegisters registers = {
		static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(frame) + sizeof(*frame)),
		frame->Gpr3Through12[0], frame->Gpr3Through12[1],
		interruptedR28, interruptedR30, frame->Lr,
		ReadSramWord(interruptedR28 + 8U), ReadSramWord(interruptedR30 + 16U),
	};
	FatalException("DataStorage", SavedStateKind::Normal, &registers);
}

extern "C" void InstructionStorage_Handler()
{
	FatalException("InstructionStorage");
}

extern "C" void ExternalInput_Handler()
{
	FatalException("ExternalInput");
}

extern "C" void Alignment_Handler()
{
	FatalException("Alignment");
}

extern "C" void Program_Handler()
{
	FatalException("Program");
}

extern "C" void FloatingPointUnavailable_Handler()
{
	FatalException("FloatingPointUnavailable");
}

extern "C" void SystemCall_Handler()
{
	FatalException("SystemCall");
}

extern "C" void AuxiliaryProcessorUnavailable_Handler()
{
	FatalException("AuxiliaryProcessorUnavailable");
}

extern "C" void FixedIntervalTimer_Handler()
{
	FatalException("FixedIntervalTimer");
}

extern "C" void WatchdogTimer_Handler()
{
	FatalException("WatchdogTimer", SavedStateKind::Critical);
}

extern "C" void DataTLBError_Handler()
{
	FatalException("DataTLBError");
}

extern "C" void InstructionTLBError_Handler()
{
	FatalException("InstructionTLBError");
}

extern "C" void Debug_Handler()
{
	FatalException("Debug", SavedStateKind::Debug);
}

extern "C" void SPEUnavailable_Handler()
{
	FatalException("SPEUnavailable");
}

extern "C" void SPEDataException_Handler()
{
	FatalException("SPEDataException");
}

extern "C" void SPERoundException_Handler()
{
	FatalException("SPERoundException");
}
