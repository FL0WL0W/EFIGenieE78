#ifndef MPC5566_SYSTEM_CLOCK_SERVICE_H
#define MPC5566_SYSTEM_CLOCK_SERVICE_H

#include <cstdint>

namespace MPC5xxx
{
	class MPC5566SystemClockService final
	{
	public:
		MPC5566SystemClockService(
			std::uint32_t referenceClockHz,
			std::uint32_t requestedSystemClockHz,
			std::uint32_t lockTimeoutIterations = 1000000U);

		bool Ready() const { return _ready; }
		std::uint32_t ReferenceClockHz() const { return _referenceClockHz; }
		std::uint32_t SystemClockHz() const;
		// MPC5566 peripheral modules, including DSPI, use fSYS directly.
		std::uint32_t PeripheralClockHz() const { return SystemClockHz(); }

	private:
		std::uint32_t _referenceClockHz = 0U;
		bool _ready = false;

		bool Configure(
			std::uint32_t requestedSystemClockHz,
			std::uint32_t lockTimeoutIterations);
	};
}

#endif
