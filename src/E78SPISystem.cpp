#include "E78SPISystem.h"

#include "MPC5xxx.h"

namespace
{
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
		2666667U,
		16U,
		MPC5xxx::SPIClockPolarity::IdleLow,
		MPC5xxx::SPIClockPhase::CaptureOnLeadingEdge,
		1000U,
		1000U,
		0U,
		false,
		false,
	};
}

namespace E78
{
	E78SPISystem::E78SPISystem()
		: _on20845SPI(
			  &DSPI_D,
			  ON20845Configuration),
		  _mpmSPI(
			  &DSPI_D,
			  MPMConfiguration),
		  _delphi28046304SPI(
			  &DSPI_B,
			  DelphiConfiguration),
		  _delphiDigitalOutputService(
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
		  DelphiDigitalOutputs(_delphiDigitalOutputService)
	{
		// E78 board-specific SPI routing, copied from the stock application's
		// PCR image. The SPI system owns every pad it uses and does not depend on
		// state inherited from the bootloader.

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

		MPM.InitializeNormalMode();
	}

	void E78SPISystem::Service()
	{
		MPC5xxx::MPC5xxxSPIService::Service(DSPI_B);
		MPC5xxx::MPC5xxxSPIService::Service(DSPI_D);
	}

	void E78SPISystem::ServiceWatchdogs()
	{
		ON20845.ServiceWatchdog();
		MPM.ServiceNormalMode();
		Delphi28046304.ServiceWatchdog();
	}
}
