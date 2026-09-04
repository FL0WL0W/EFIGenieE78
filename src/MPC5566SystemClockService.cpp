#include "MPC5566SystemClockService.h"

#include "MPC5xxx.h"

#include <cstdint>

namespace
{
	constexpr std::uint32_t MinimumReferenceClockHz = 8000000U;
	constexpr std::uint32_t MaximumReferenceClockHz = 20000000U;
	constexpr std::uint32_t MinimumPhaseDetectorClockHz = 4000000U;
	constexpr std::uint32_t MinimumICOClockHz = 48000000U;
	constexpr std::uint32_t MaximumMPC5566SystemClockHz = 147000000U;

	constexpr std::uint32_t PredividerMask = 0x70000000U;
	constexpr std::uint32_t MultiplierMask = 0x0F800000U;
	constexpr std::uint32_t ReducedDividerMask = 0x00380000U;
	constexpr std::uint32_t LossOfLockResetEnable = 0x00020000U;
	constexpr std::uint32_t LossOfLockInterruptEnable = 0x00004000U;
	constexpr std::uint32_t ModulationDepthMask = 0x00000C00U;

	struct PLLSettings
	{
		std::uint8_t predivider = 0U;
		std::uint8_t multiplier = 0U;
		std::uint8_t reducedDivider = 0U;
		std::uint32_t clockHz = 0U;
		bool valid = false;
	};

	PLLSettings FindSettings(
		std::uint32_t referenceClockHz,
		std::uint32_t requestedClockHz)
	{
		PLLSettings best;
		for (std::uint8_t predivider = 0U; predivider <= 4U; ++predivider)
		{
			const std::uint32_t phaseDetectorClock =
				referenceClockHz / (static_cast<std::uint32_t>(predivider) + 1U);
			if (phaseDetectorClock < MinimumPhaseDetectorClockHz)
				continue;

			for (std::uint8_t multiplier = 0U; multiplier <= 31U; ++multiplier)
			{
				const std::uint64_t icoClock =
					static_cast<std::uint64_t>(referenceClockHz) *
					(static_cast<std::uint32_t>(multiplier) + 4U) /
					(static_cast<std::uint32_t>(predivider) + 1U);
				if (icoClock < MinimumICOClockHz ||
					icoClock > MaximumMPC5566SystemClockHz)
					continue;

				for (std::uint8_t reducedDivider = 0U;
					reducedDivider < 7U;
					++reducedDivider)
				{
					const std::uint32_t candidate = static_cast<std::uint32_t>(
						icoClock >> reducedDivider);
					if (candidate > requestedClockHz || candidate <= best.clockHz)
						continue;
					best.predivider = predivider;
					best.multiplier = multiplier;
					best.reducedDivider = reducedDivider;
					best.clockHz = candidate;
					best.valid = true;
				}
			}
		}
		return best;
	}

	std::uint32_t WithPLLSettings(
		std::uint32_t control,
		const PLLSettings& settings,
		std::uint8_t reducedDivider)
	{
		control &= ~(PredividerMask | MultiplierMask | ReducedDividerMask |
			LossOfLockResetEnable | LossOfLockInterruptEnable |
			ModulationDepthMask);
		control |= static_cast<std::uint32_t>(settings.predivider) << 28U;
		control |= static_cast<std::uint32_t>(settings.multiplier) << 23U;
		control |= static_cast<std::uint32_t>(reducedDivider) << 19U;
		return control;
	}
}

namespace MPC5xxx
{
	MPC5566SystemClockService::MPC5566SystemClockService(
		std::uint32_t referenceClockHz,
		std::uint32_t requestedSystemClockHz,
		std::uint32_t lockTimeoutIterations)
		: _referenceClockHz(referenceClockHz)
	{
		_ready = Configure(
			requestedSystemClockHz,
			lockTimeoutIterations);
	}

	bool MPC5566SystemClockService::Configure(
		std::uint32_t requestedSystemClockHz,
		std::uint32_t lockTimeoutIterations)
	{
		if (_referenceClockHz < MinimumReferenceClockHz ||
			_referenceClockHz > MaximumReferenceClockHz ||
			requestedSystemClockHz == 0U ||
			requestedSystemClockHz > MaximumMPC5566SystemClockHz ||
			lockTimeoutIterations == 0U)
			return false;
		if (FMPLL.SYNSR.B.LOCK != 0U &&
			SystemClockHz() == requestedSystemClockHz)
			return true;

		const PLLSettings settings = FindSettings(
			_referenceClockHz,
			requestedSystemClockHz);
		if (!settings.valid)
			return false;

		const std::uint32_t originalControl = FMPLL.SYNCR.R;
		const std::uint32_t failureEnables = originalControl &
			(LossOfLockResetEnable | LossOfLockInterruptEnable);
		const std::uint8_t transitionDivider = static_cast<std::uint8_t>(
			settings.reducedDivider + 1U);

		// The reference manual requires modulation and loss-of-lock actions to
		// be disabled while PREDIV/MFD change. Start one RFD step below the final
		// frequency to avoid a transient overspeed while the PLL reacquires lock.
		FMPLL.SYNCR.R = WithPLLSettings(
			originalControl,
			settings,
			transitionDivider);
		asm volatile("mbar 0" ::: "memory");

		while (FMPLL.SYNSR.B.LOCK == 0U)
		{
			if (--lockTimeoutIterations == 0U)
				return false;
		}

		std::uint32_t finalControl = WithPLLSettings(
			originalControl,
			settings,
			settings.reducedDivider);
		finalControl |= failureEnables;
		FMPLL.SYNCR.R = finalControl;
		asm volatile("mbar 0" ::: "memory");
		return true;
	}

	std::uint32_t MPC5566SystemClockService::SystemClockHz() const
	{
		if (_referenceClockHz == 0U)
			return 0U;
		const std::uint32_t predivider = FMPLL.SYNCR.B.PREDIV + 1U;
		const std::uint32_t multiplier = FMPLL.SYNCR.B.MFD + 4U;
		const std::uint32_t reducedDivider = 1U << FMPLL.SYNCR.B.RFD;
		return static_cast<std::uint32_t>(
			static_cast<std::uint64_t>(_referenceClockHz) * multiplier /
			(predivider * reducedDivider));
	}
}
