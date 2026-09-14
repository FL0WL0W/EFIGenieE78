#include "E78System.h"
#include "UDSService.h"

#include <cstddef>
#include <cstdint>

using namespace EmbeddedIOServices;
using namespace MPC5xxx;

extern "C" [[noreturn]] void ExitToBootloaderUploadRoutine();

extern "C" __attribute__((weak)) bool WriteToFlash(
	std::uint32_t address,
	const std::uint8_t* data,
	std::size_t length)
{
	(void)address;
	(void)data;
	(void)length;
	return false;
}

namespace
{
	constexpr std::uint32_t LoopPeriodTimebaseTicks = 384000;
	// At a 32 MHz timebase this produces an intentionally slow 100-baud UART.
	// One 9N1 word therefore takes 110 ms. Every output transmits its complete
	// connector-pin encoding simultaneously, followed by a 500 ms idle period.
	constexpr std::uint32_t UARTBitTimebaseTicks = 320000U;
	constexpr std::uint32_t UARTInterPinTimebaseTicks = 16000000U;

	// Every connector pin supported as an output by E78DigitalService. This
	// includes direct MCU GPIO/eTPU pads, the 21 Delphi DSI outputs, and the
	// three Delphi outputs carried in the DSPI-B status command.
	constexpr digitalpin_t OutputList[] = {
		// X1
		114U, 127U, 128U, 140U, 141U, 147U, 150U,
		151U, 152U, 153U, 154U, 155U, 156U,
		// X2
		201U, 202U, 203U, 204U, 205U, 206U, 207U,
		208U, 209U, 210U, 211U, 212U, 214U, 215U,
		216U, 217U, 218U, 232U, 233U, 234U, 252U,
		253U, 254U, 255U, 272U,
		// X3
		303U, 304U, 305U, 306U, 307U, 308U, 309U,
		310U, 311U, 312U, 313U, 314U, 315U, 316U,
		317U, 332U,
	};
	constexpr size_t OutputCount = sizeof(OutputList) / sizeof(OutputList[0]);

	void ServiceCoreWatchdog()
	{
		const std::uint32_t watchdogService = 0x40000000U;
		asm volatile(
			"isync\n"
			"mtspr 336, %0\n"
			"isync\n"
			:
			: "r"(watchdogService)
			: "memory");
	}

	struct SlowUARTSweep
	{
		static constexpr std::uint8_t TransitionCount = 12U;
		static constexpr std::uint8_t IdleTransition = 0U;
		static constexpr std::uint8_t StartTransition = 1U;
		static constexpr std::uint8_t FirstDataTransition = 2U;
		static constexpr std::uint8_t StopTransition = 11U;

		IDigitalService& digitalService;
		volatile bool frameComplete = false;
		Task transitionTasks[TransitionCount];
		Task frameCompleteTask;

		SlowUARTSweep(IDigitalService& digital)
			: digitalService(digital),
			  transitionTasks{
				  Task([this]() { ApplyTransition(0U); }),
				  Task([this]() { ApplyTransition(1U); }),
				  Task([this]() { ApplyTransition(2U); }),
				  Task([this]() { ApplyTransition(3U); }),
				  Task([this]() { ApplyTransition(4U); }),
				  Task([this]() { ApplyTransition(5U); }),
				  Task([this]() { ApplyTransition(6U); }),
				  Task([this]() { ApplyTransition(7U); }),
				  Task([this]() { ApplyTransition(8U); }),
				  Task([this]() { ApplyTransition(9U); }),
				  Task([this]() { ApplyTransition(10U); }),
				  Task([this]() { ApplyTransition(11U); }),
			  },
			  frameCompleteTask([this]() { frameComplete = true; })
		{
		}

		void ApplyTransition(const std::uint8_t transition)
		{
			for (size_t pin = 0U; pin < OutputCount; ++pin)
			{
				bool value;
				if (transition == IdleTransition || transition == StopTransition)
					value = true;
				else if (transition == StartTransition)
					value = false;
				else
				{
					// UART sends the complete connector-pin encoding LSB first.
					const std::uint8_t dataBit = static_cast<std::uint8_t>(
						transition - FirstDataTransition);
					value = ((OutputList[pin] >> dataBit) & 1U) != 0U;
				}

				digitalService.WritePin(OutputList[pin], value);
				if (transition == IdleTransition)
					digitalService.InitPin(OutputList[pin], Out);
			}
		}

		void ScheduleFrame(ITimerService& timerService, const tick_t startTick)
		{
			for (std::uint8_t transition = 0U;
				transition < TransitionCount;
				++transition)
			{
				timerService.ScheduleTask(
					&transitionTasks[transition],
					startTick + transition * UARTBitTimebaseTicks);
			}

			timerService.ScheduleTask(
				&frameCompleteTask,
				startTick + TransitionCount * UARTBitTimebaseTicks +
					UARTInterPinTimebaseTicks);
		}

		void Service(ITimerService& timerService)
		{
			if (!frameComplete) return;
			frameComplete = false;
			ScheduleFrame(timerService, timerService.GetTick() + 1U);
		}
	};
}

extern "C" int main()
{
	asm("wrteei 1");

	E78::E78System system;
	system.Initialize();

	ICommunicationService* const isotp = system.ISOTPService;
	const E78::UDSMemoryRegion udsReadRegions[] = {
		{0x00000000U, 0x00003FE0U, true},
		{0x00004000U, 0x0001BFE0U, true},
		{0x00020000U, 0x003E0000U, true},
		{0x40000000U, 0x00040000U, false},
	};
	const E78::UDSMemoryRegion udsWriteRegions[] = {
		{0x00000000U, 0x00400000U, true},
		{0x40000000U, 0x00040000U, false},
	};
	E78::UDSService uds(
		*isotp,
		udsReadRegions,
		sizeof(udsReadRegions) / sizeof(udsReadRegions[0]),
		udsWriteRegions,
		sizeof(udsWriteRegions) / sizeof(udsWriteRegions[0]),
		WriteToFlash,
		ExitToBootloaderUploadRoutine);

	const std::uint8_t alive = 0x99U;
	isotp->Send(&alive, 1U);
	std::uint32_t loopStart = system.TimerService.GetTick();
	SlowUARTSweep uartSweep(system.DigitalService);
	uartSweep.ScheduleFrame(
		system.TimerService,
		loopStart + UARTInterPinTimebaseTicks);
	while (true)
	{
		system.Service();

		uartSweep.Service(system.TimerService);

		const std::uint32_t now = system.TimerService.GetTick();
		if (static_cast<std::uint32_t>(now - loopStart) <
			LoopPeriodTimebaseTicks)
			continue;

		loopStart = now;
		ServiceCoreWatchdog();
		system.ServiceWatchdogs();
	}
}
