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

	std::uint32_t ReadTimebase()
	{
		std::uint32_t value;
		asm volatile("mftb %0" : "=r"(value));
		return value;
	}

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
		std::uint8_t frameBit = 0U;
		bool transmitting = false;
		std::uint32_t deadline = 0U;
	};

	void BeginUARTFrame(
		SlowUARTSweep& sweep,
		IDigitalService& digitalService,
		std::uint32_t now)
	{
		for (size_t pin = 0U; pin < OutputCount; ++pin)
		{
			// Establish idle high for a complete bit time before emitting the
			// start bit so the decoder can establish valid UART framing.
			digitalService.WritePin(OutputList[pin], true);
			digitalService.InitPin(OutputList[pin], Out);
		}
		sweep.frameBit = 0xFFU; // pre-frame idle-high interval
		sweep.transmitting = true;
		sweep.deadline = now + UARTBitTimebaseTicks;
	}

	void ServiceUARTSweep(
		SlowUARTSweep& sweep,
		IDigitalService& digitalService,
		std::uint32_t now)
	{
		if (static_cast<std::int32_t>(now - sweep.deadline) < 0) return;

		if (!sweep.transmitting)
		{
			BeginUARTFrame(sweep, digitalService, now);
			return;
		}

		if (sweep.frameBit == 0xFFU)
		{
			for (size_t pin = 0U; pin < OutputCount; ++pin)
				digitalService.WritePin(OutputList[pin], false); // start bit
			sweep.frameBit = 0U;
			sweep.deadline += UARTBitTimebaseTicks;
			return;
		}

		++sweep.frameBit;
		if (sweep.frameBit <= 9U)
		{
			for (size_t pin = 0U; pin < OutputCount; ++pin)
			{
				// UART sends the full connector-pin encoding LSB first. Nine
				// data bits represent every X1, X2, and X3 connector pin.
				const std::uint16_t value = OutputList[pin];
				digitalService.WritePin(
					OutputList[pin],
					((value >> (sweep.frameBit - 1U)) & 1U) != 0U);
			}
			sweep.deadline += UARTBitTimebaseTicks;
			return;
		}

		if (sweep.frameBit == 10U)
		{
			for (size_t pin = 0U; pin < OutputCount; ++pin)
				digitalService.WritePin(OutputList[pin], true); // stop bit
			sweep.deadline += UARTBitTimebaseTicks;
			return;
		}

		sweep.transmitting = false;
		sweep.deadline = now + UARTInterPinTimebaseTicks;
	}
}

extern "C" int main()
{
	asm("wrteei 0");

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

	asm volatile(
		"mbar\n"
		"wrteei 1\n"
		"isync\n"
		:
		:
		: "memory");

	const std::uint8_t alive = 0x99U;
	isotp->Send(&alive, 1U);
	std::uint32_t loopStart = ReadTimebase();
	SlowUARTSweep uartSweep;
	uartSweep.deadline = loopStart + UARTInterPinTimebaseTicks;
	while (true)
	{
		system.Service();

		const std::uint32_t now = ReadTimebase();
		ServiceUARTSweep(uartSweep, system.DigitalService, now);

		if (static_cast<std::uint32_t>(now - loopStart) <
			LoopPeriodTimebaseTicks)
			continue;

		loopStart = now;
		ServiceCoreWatchdog();
		system.ServiceWatchdogs();
	}
}
