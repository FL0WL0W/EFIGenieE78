#include "E78SPISystem.h"
#include "MPC5xxxAnalogService.h"
#include "MPC5xxxDigitalService.h"
#include "MPC5xxxFlexCAN2Service.h"
#include "MPC55xxSystemClockService.h"
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
	constexpr std::uint32_t LoopPeriodTimebaseTicks = 0x00080000U;
	constexpr std::uint32_t IgnitionTogglePeriodTimebaseTicks = 0x0004E200U;
	constexpr std::uint32_t IgnitionToggleWaitToCaptureTimebaseTicks = 0x0002E200U;
	constexpr std::uint32_t AnalogDetectionPeriodTimebaseTicks = 0x00F42400U;
	constexpr analogpin_t FirstAnalogChannel = 64U;
	constexpr std::size_t AnalogChannelCount = 32U;
	constexpr float MinimumCorrelatedVoltageChange = 0.20F;
	constexpr digitalpin_t FirstInjectorPin = 132U;
	constexpr digitalpin_t FirstIgnitionPin = 167U;
	constexpr std::size_t EngineOutputCount = 6U;
	constexpr std::uint16_t FlexCANARxVectorFirst = 155U;
	constexpr std::uint16_t FlexCANARxHighVector = 171U;
	constexpr std::uint16_t FlexCANATxHighVector = 172U;
	constexpr std::uint8_t FlexCANInterruptPriority = 1U;
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
}

extern "C" int main()
{
	asm("wrteei 0");

	MPC55xxSystemClockService::Initialize(8000000U, 128000000U);
	E78::E78SPISystem spiSystem;
	spiSystem.ON20845.SendOutputConfiguration();
	spiSystem.DelphiDigitalOutputs.InitPin(4U, Out);
	spiSystem.DelphiDigitalOutputs.WritePin(4U, true);
	spiSystem.Delphi28046304.RequestIdentification(nullptr);
	spiSystem.Delphi28046304.RequestIdentification(nullptr);
	spiSystem.Delphi28046304.SendRevision4Configuration(nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F1AU, 0x0082U, nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F1DU, 0x1450U, nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F1DU, 0x04F0U, nullptr);
	spiSystem.Delphi28046304.RequestDiagnostic(nullptr);
	spiSystem.Delphi28046304.SendCommand(0x0F14U, 0x3E20U, nullptr);
	MPC5xxxDigitalService digitalService;
	for (std::size_t channel = 0U; channel < EngineOutputCount; ++channel)
	{
		const digitalpin_t injector = static_cast<digitalpin_t>(
			FirstInjectorPin + channel);
		const digitalpin_t ignition = static_cast<digitalpin_t>(
			FirstIgnitionPin + channel);
		digitalService.WritePin(injector, false);
		digitalService.WritePin(ignition, false);
		digitalService.InitPin(injector, Out);
		digitalService.InitPin(ignition, Out);
	}
	// E78 routes the eQADC external-multiplexer address outputs MA0-MA2 to
	// SIU pads 215-217. This board-level configuration does not belong in the
	// generic MPC5xxx analog service.
	SIU.PCR[215U].B.PA = 2U;
	SIU.PCR[216U].B.PA = 2U;
	SIU.PCR[217U].B.PA = 2U;
	SIU.PCR[217U].B.WPE = 0U;
	MPC5xxxAnalogService analogService(
		EQADC,
		5.0F);

	const uint8_t busNumber = MPC5xxxFlexCAN2Service::Initialize(CAN_A, CANBaudRate::Kbps500);
	ICommunicationService* const isotp = MPC5xxxFlexCAN2Service::Instance().GetISOTPService(
		{0x7E0U, busNumber},
		{0x7E8U, busNumber});
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

	CAN_A.IMRH.R = 0U;
	CAN_A.IMRL.R = 0U;
	CAN_A.CR.B.BOFFMSK = 0U;
	CAN_A.CR.B.ERRMSK = 0U;
	CAN_A.CR.B.TWRNMSK = 0U;
	CAN_A.CR.B.RWRNMSK = 0U;
	CAN_A.MCR.B.WRNEN = 0U;
	CAN_A.IFRH.R = 0xFFFFFFFFU;
	CAN_A.IFRL.R = 0xFFFFFFFFU;
	for (std::uint16_t vector = FlexCANARxVectorFirst;
		vector < FlexCANARxVectorFirst + 16U;
		++vector)
	{
		INTC.PSR[vector].R = FlexCANInterruptPriority;
	}
	INTC.PSR[FlexCANARxHighVector].R = FlexCANInterruptPriority;
	INTC.PSR[FlexCANATxHighVector].R = FlexCANInterruptPriority;
	CAN_A.IMRL.R = 0xFFFFFFFFU;
	CAN_A.IMRH.R = 0xFFFFFFFFU;
	asm volatile(
		"mbar\n"
		"wrteei 1\n"
		"isync\n"
		:
		:
		: "memory");

	const std::uint8_t alive = 0x99U;
	isotp->Send(&alive, 1U);
	uint32_t misses[AnalogChannelCount] = {};
    uint32_t checks = 0;
	bool injectorOutputState = false;
	bool ignitionOutputState = false;
	std::uint32_t loopStart = ReadTimebase();
	std::uint32_t ignitionToggleStart = loopStart;
	std::uint32_t analogWindowStart = loopStart;
	while (true)
	{
		spiSystem.Service();

		const std::uint32_t now = ReadTimebase();

		if (static_cast<std::uint32_t>(now - ignitionToggleStart) >=
			IgnitionToggleWaitToCaptureTimebaseTicks)
        {
            for (std::size_t channelIndex = 0U;
                channelIndex < AnalogChannelCount;
                ++channelIndex)
            {
                const analogpin_t channel = static_cast<analogpin_t>(
                    FirstAnalogChannel + channelIndex);
                const float voltage = analogService.ReadPin(
                    channel);
				// Keep the cooperative DSPI FIFOs supplied during the polling ADC
				// scan. In particular, an MPM packet is longer than DSPI's four-entry
				// hardware FIFO.
				spiSystem.Service();
                if (ignitionOutputState)
                {
                    if(voltage < 2.5F)
                        ++misses[channelIndex];
                }
                else
                {
                    if(voltage > 2.5F)
                        ++misses[channelIndex];
                }
            }
            checks++;
        }

		if (static_cast<std::uint32_t>(now - analogWindowStart) >=
			AnalogDetectionPeriodTimebaseTicks)
		{
			analogWindowStart = now;
			for (std::size_t channelIndex = 0U;
				channelIndex < AnalogChannelCount;
				++channelIndex)
			{
                uint8_t missPercent = (misses[channelIndex] * 100) / checks;
				if (missPercent < 5 && isotp->Ready())
				{
					const std::uint8_t detection[] = {
						0xA0U,
						static_cast<std::uint8_t>(
							FirstAnalogChannel + channelIndex),
					};
					isotp->Send(detection, sizeof(detection));
				}

				misses[channelIndex] = 0U;
			}
            checks = 0;
		}

		if (static_cast<std::uint32_t>(now - ignitionToggleStart) >=
			IgnitionTogglePeriodTimebaseTicks)
		{
			ignitionToggleStart = now;
			ignitionOutputState = !ignitionOutputState;
			for (std::size_t channel = 0U; channel < EngineOutputCount; ++channel)
			{
				digitalService.WritePin(
					static_cast<digitalpin_t>(FirstIgnitionPin + channel),
					ignitionOutputState);
			}
		}

		if (static_cast<std::uint32_t>(now - loopStart) <
			LoopPeriodTimebaseTicks)
			continue;

		loopStart = now;
		ServiceCoreWatchdog();
		spiSystem.ServiceWatchdogs();
		injectorOutputState = !injectorOutputState;
		for (std::size_t channel = 0U; channel < EngineOutputCount; ++channel)
		{
			digitalService.WritePin(
				static_cast<digitalpin_t>(FirstInjectorPin + channel),
				injectorOutputState);
		}
	}
}
