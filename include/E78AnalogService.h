#include "MPC5xxxAnalogService.h"

#ifndef E78ANALOGSERVICE_H
#define E78ANALOGSERVICE_H

namespace E78
{
	/**
	 * E78 connector-level digital I/O.
	 *
	 * Connector pins are encoded as connector * 100 + cavity. For example,
	 * X1-53 is pin 153, X2-2 is pin 202, and X3-15 is pin 315.
	 */
	class E78AnalogService final : public EmbeddedIOServices::IAnalogService
	{
	private:
		MPC5xxx::MPC5xxxAnalogService _mcuAnalogService;

		static EmbeddedIOServices::analogpin_t TranslatePin(EmbeddedIOServices::analogpin_t connectorPin);

	public:
		E78AnalogService();

		void InitPin(EmbeddedIOServices::analogpin_t pin) override;
		float ReadPin(EmbeddedIOServices::analogpin_t pin) override;
	};
}

#endif
