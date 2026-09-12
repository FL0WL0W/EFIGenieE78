#include "E78System.h"

#include "MPC5xxx.h"

#include <cstddef>
#include <cstdint>

namespace
{
	constexpr std::uint16_t FlexCANARxVectorFirst = 155U;
	constexpr std::uint16_t FlexCANARxHighVector = 171U;
	constexpr std::uint16_t FlexCANATxHighVector = 172U;
	constexpr std::uint8_t FlexCANInterruptPriority = 1U;

	struct StockGPIOOutput
	{
		std::uint16_t pin;
		std::uint16_t pcr;
		bool initialHigh;
	};

	// Direct GPIO outputs from the stock E78 application's SIU initialization
	// image at 0x000C45DE. Peripheral-function outputs (DSPI, CAN, eTPU, etc.)
	// are deliberately not included here. GPIO205 is the only direct output
	// initialized high by the stock GPDO image and is a strong candidate for a
	// discrete Delphi ASIC enable.
	constexpr StockGPIOOutput StockGPIOOutputs[] = {
		{1U, 0x0200U, false}, {2U, 0x0300U, false},
		{3U, 0x0300U, false}, {8U, 0x0300U, false},
		{27U, 0x0300U, false}, {60U, 0x0300U, false},
		{61U, 0x0300U, false}, {70U, 0x0300U, false},
		{71U, 0x0300U, false}, {73U, 0x0210U, false},
		{74U, 0x0300U, false}, {88U, 0x0304U, false},
		{90U, 0x0314U, false}, {94U, 0x0304U, false},
		{97U, 0x0304U, false}, {101U, 0x0204U, false},
		{108U, 0x0304U, false}, {111U, 0x0304U, false},
		{112U, 0x0304U, false}, {113U, 0x0304U, false},
		{114U, 0x0210U, false}, {119U, 0x0210U, false},
		{120U, 0x0304U, false}, {124U, 0x0304U, false},
		{125U, 0x0304U, false}, {126U, 0x0304U, false},
		{127U, 0x0304U, false}, {128U, 0x0304U, false},
		{129U, 0x0304U, false}, {140U, 0x0304U, false},
		{141U, 0x0304U, false}, {142U, 0x0304U, false},
		{143U, 0x0310U, false}, {144U, 0x0304U, false},
		{158U, 0x0304U, false}, {159U, 0x0310U, false},
		{160U, 0x0310U, false}, {161U, 0x0304U, false},
		{162U, 0x0304U, false}, {176U, 0x0310U, false},
		{179U, 0x0304U, false}, {180U, 0x0310U, false},
		{181U, 0x0304U, false}, {182U, 0x0310U, false},
		{183U, 0x0304U, false}, {184U, 0x0304U, false},
		{187U, 0x0310U, false}, {188U, 0x0310U, false},
		{189U, 0x0304U, false}, {191U, 0x0310U, false},
		{192U, 0x0304U, false}, {193U, 0x0304U, false},
		{194U, 0x0304U, false}, {195U, 0x0304U, false},
		{197U, 0x0310U, false}, {198U, 0x0304U, false},
		{205U, 0x0210U, true}, {206U, 0x0300U, false},
		{207U, 0x0300U, false},
	};

	void ConfigureStockGPIOOutputs()
	{
		// The stock firmware writes the complete GPDO image before the PCR
		// image. Preserve that ordering so enabling an output buffer cannot
		// produce a transient level.
		for (const StockGPIOOutput& output : StockGPIOOutputs)
			SIU.GPDO[output.pin].B.PDO = output.initialHigh ? 1U : 0U;
		for (const StockGPIOOutput& output : StockGPIOOutputs)
			SIU.PCR[output.pin].R = output.pcr;
	}

	constexpr MPC5xxx::MPC5xxxSPIServiceConfiguration ON20845Configuration = {
		0U,
		4800000U,
		16U,
		MPC5xxx::SPIClockPolarity::IdleLow,
		MPC5xxx::SPIClockPhase::CaptureOnLeadingEdge,
		500U,
		1500U,
		3000U,
		false,
		false,
	};

	constexpr MPC5xxx::MPC5xxxSPIServiceConfiguration MPMConfiguration = {
		1U,
		125000U,
		8U,
		MPC5xxx::SPIClockPolarity::IdleLow,
		MPC5xxx::SPIClockPhase::CaptureOnTrailingEdge,
		875U,
		320000U,
		110U,
		false,
		false,
	};

	constexpr MPC5xxx::MPC5xxxSPIServiceConfiguration DelphiConfiguration = {
		0U,
		5333333U,
		16U,
		MPC5xxx::SPIClockPolarity::IdleLow,
		MPC5xxx::SPIClockPhase::CaptureOnLeadingEdge,
		1000U,
		1000U,
		0U,
		false,
		false,
	};

	std::uint8_t InitializeFlexCANA()
	{
		return MPC5xxx::MPC5xxxFlexCAN2Service::Initialize(
			CAN_A,
			MPC5xxx::CANBaudRate::Kbps500);
	}

	void ConfigureFlexCANAInterrupts()
	{
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
	}
}

namespace E78
{
	E78System::SystemClockInitializer::SystemClockInitializer()
	{
		MPC5xxx::MPC55xxSystemClockService::Initialize(
			8000000U,
			128000000U);
	}

	E78System::E78System()
		: _systemClock(),
		  _on20845SPI(
			  &DSPI_D,
			  ON20845Configuration),
		  _mpmSPI(
			  &DSPI_D,
			  MPMConfiguration),
		  _delphi28046304SPI(
			  &DSPI_B,
			  DelphiConfiguration),
		  _canBusNumber(InitializeFlexCANA()),
		  ISOTPService(
			  MPC5xxx::MPC5xxxFlexCAN2Service::Instance().GetISOTPService(
				  {0x7E0U, _canBusNumber},
				  {0x7E8U, _canBusNumber})),
		  MPCDigitalService(),
		  DelphiDigitalOutputService(
			  &DSPI_A,
			  &DSPI_C,
			  0x113F0C01U,
			  0x913F0C01U,
			  0x22003341U,
			  0x7A003341U,
			  0x94080001U,
			  0x001FFFFFU,
			  0U),
		  ON20845(_on20845SPI),
		  MPM(_mpmSPI),
		  Delphi28046304(_delphi28046304SPI),
		  DigitalService(
			  MPCDigitalService,
			  DelphiDigitalOutputService,
			  Delphi28046304)
	{
		// E78 board-specific SPI routing, copied from the stock application's
		// PCR image. E78System owns every pad it uses and does not depend on
		// state inherited from the bootloader.
		ConfigureStockGPIOOutputs();

		// DSPI-D: ON20845 on PCS0, MPM on PCS1, and the stock PCS3 route.
		SIU.PCR[87U].R = 0x0A04U;  // PCSD3
		SIU.PCR[91U].R = 0x0A04U;  // PCSD1 / MPM
		SIU.PCR[98U].R = 0x0A04U;  // SCKD
		SIU.PCR[99U].R = 0x0914U;  // SIND
		SIU.PCR[100U].R = 0x0A14U; // SOUTD
		SIU.PCR[106U].R = 0x0A04U; // PCSD0 / ON20845

		// DSPI-B: Delphi 28046304 command/diagnostic interface.
		SIU.PCR[102U].R = 0x0604U; // SCKB
		SIU.PCR[103U].R = 0x0514U; // SINB
		SIU.PCR[104U].R = 0x0614U; // SOUTB
		SIU.PCR[105U].R = 0x0604U; // PCSB0

		// DSI chain: DSPI-A supplies serialized data while DSPI-C supplies the
		// clock and chip select to the Delphi ASIC.
		SIU.PCR[95U].R = 0x060CU;  // SOUTA
		SIU.PCR[109U].R = 0x0A0CU; // SCKC
		SIU.PCR[110U].R = 0x0A0CU; // PCSC0

		// Dedicated reference clock supplied to the Delphi ASIC. Match the
		// measured stock E78 firmware: at 128 MHz, ENGDIV=8 produces 8 MHz:
		//     fENGCLK = fSYS / (2 * ENGDIV)
		SIU.PCR[214U].R = 0x0200U; // Enable the dedicated ENGCLK output buffer.
		SIU.ECCR.R = 0x00000801U;  // ENGCLK /16; retain CLKOUT divide-by-2.

		ConfigureFlexCANAInterrupts();
		MPM.InitializeNormalMode();
	}

	void E78System::Initialize()
	{
		if (_startupStarted)
			return;
		_startupStarted = true;
		Delphi28046304.RequestIdentification(
			[this](const std::uint16_t*, std::size_t) {
				ContinueAfterFirstIdentification();
			});
	}

	void E78System::ContinueAfterFirstIdentification()
	{
		Delphi28046304.RequestIdentification(
			[this](const std::uint16_t*, std::size_t) {
				ContinueAfterSecondIdentification();
			});
	}

	void E78System::ContinueAfterSecondIdentification()
	{
		// Stock starts the 21-bit DSI chain only after both identification
		// transactions have completed.
		DelphiDigitalOutputService.Start();
		Delphi28046304.SendRevision4Configuration(
			[this](const std::uint16_t*, std::size_t) {
				ContinueAfterConfiguration();
			});
	}

	void E78System::ContinueAfterConfiguration()
	{
		// Revision-3 post-configuration prefix; it appears as a third
		// 0E1B/0000 transaction on the wire.
		Delphi28046304.RequestIdentification(
			[this](const std::uint16_t*, std::size_t) {
				ContinueAfterRevisionPrefix();
			});
	}

	void E78System::ContinueAfterRevisionPrefix()
	{
		// These transitions happen after DSI is active in the stock runtime.
		// Keep their rising edges on the same side of DSI initialization.
		SIU.GPDO[160U].B.PDO = 1U;
		SIU.GPDO[197U].B.PDO = 1U;

		// The companion output enable also follows the Delphi configuration in
		// stock firmware, although it remains an independent DSPI-D device.
		ON20845.SendOutputConfiguration();

		Delphi28046304.SendCommand(0x0F1AU, 0x0082U, nullptr);
		Delphi28046304.SendCommand(0x0F1DU, 0x1450U, nullptr);
		Delphi28046304.SendCommand(0x0F1DU, 0x04F0U, nullptr);
		Delphi28046304.RequestDiagnostic(
			[this](const std::uint16_t*, std::size_t) {
				_startupComplete = true;
			});
	}

	void E78System::Service()
	{
		MPC5xxx::MPC5xxxSPIService::Service(DSPI_B);
		MPC5xxx::MPC5xxxSPIService::Service(DSPI_D);
	}

	static uint32_t everyFourthCall = 0U;
	void E78System::ServiceWatchdogs()
	{
		ON20845.ServiceWatchdog();
		if (_startupComplete)
			Delphi28046304.ServiceWatchdog();
		if(everyFourthCall %4 ==0)
			MPM.ServiceNormalMode();
		everyFourthCall++;
	}
}
