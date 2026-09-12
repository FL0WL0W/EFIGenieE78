#ifndef E78SYSTEM_H
#define E78SYSTEM_H

#include "Delphi28046304Device.h"
#include "DelphiDSIDigitalService.h"
#include "E78DigitalService.h"
#include "ICommunicationService.h"
#include "MPC5xxxDigitalService.h"
#include "MPC5xxxFlexCAN2Service.h"
#include "MPC5xxxSPIService.h"
#include "MPC55xxSystemClockService.h"
#include "MPMDevice.h"
#include "MPMSPIService.h"
#include "ON20845-007Device.h"

#include <cstdint>

namespace E78
{
	class E78System final
	{
	private:
		struct SystemClockInitializer
		{
			SystemClockInitializer();
		};

		SystemClockInitializer _systemClock;
		MPC5xxx::MPC5xxxSPIService _on20845SPI;
		MPMSPIService _mpmSPI;
		MPC5xxx::MPC5xxxSPIService _delphi28046304SPI;
		std::uint8_t _canBusNumber;
		bool _startupStarted = false;
		bool _startupComplete = false;

		void ContinueAfterFirstIdentification();
		void ContinueAfterSecondIdentification();
		void ContinueAfterConfiguration();
		void ContinueAfterRevisionPrefix();

	public:
		EmbeddedIOServices::ICommunicationService* const ISOTPService;
		MPC5xxx::MPC5xxxDigitalService MPCDigitalService;
		MPC5xxx::DelphiDSIDigitalService DelphiDigitalOutputService;
		ON20845_007Device ON20845;
		MPMDevice MPM;
		Delphi28046304Device Delphi28046304;
		E78DigitalService DigitalService;

		E78System();

		void Initialize();
		void Service();
		void ServiceWatchdogs();
	};
}

#endif
