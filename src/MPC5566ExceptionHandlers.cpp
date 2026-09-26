#include "ICommunicationService.h"
#include "MPC5xxx.h"
#include "MPC5xxxFlexCAN2Service.h"

#include <cstdint>
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
		const SavedStateKind stateKind = SavedStateKind::Normal)
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
					"FATAL %s PC=%08lX MSR=%08lX DEAR=%08lX ESR=%08lX MCSR=%08lX\n",
					name,
					static_cast<unsigned long>(snapshot.ProgramCounter),
					static_cast<unsigned long>(snapshot.MachineState),
					static_cast<unsigned long>(snapshot.DataExceptionAddress),
					static_cast<unsigned long>(snapshot.ExceptionSyndrome),
					static_cast<unsigned long>(snapshot.MachineCheckSyndrome));
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

extern "C" void DataStorage_Handler()
{
	FatalException("DataStorage");
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
