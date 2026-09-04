#include "MPMSPIService.h"

namespace E78
{
	MPC5xxx::SPIFrameTiming MPMSPIService::TimingForFrame(
		std::size_t frameIndex) const
	{
		if (frameIndex == 0U)
			return {875U, 320000U, 110U};
		if (frameIndex == 1U)
			return {875U, 96000U, 14000U};
		return {875U, 28000U, 14000U};
	}
}
