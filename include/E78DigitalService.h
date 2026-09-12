#ifndef E78DIGITALSERVICE_H
#define E78DIGITALSERVICE_H

#include "DelphiDSIDigitalService.h"
#include "Delphi28046304Device.h"
#include "IDigitalService.h"
#include "MPC5xxxDigitalService.h"

namespace E78
{
	/**
	 * E78 connector-level digital I/O.
	 *
	 * Connector pins are encoded as connector * 100 + cavity. For example,
	 * X1-53 is pin 153, X2-2 is pin 202, and X3-15 is pin 315.
	 */
	class E78DigitalService final : public EmbeddedIOServices::IDigitalService
	{
	private:
		MPC5xxx::MPC5xxxDigitalService& _mcuDigitalService;
		MPC5xxx::DelphiDSIDigitalService& _dsiDigitalService;
		Delphi28046304Device& _delphiDevice;

		static EmbeddedIOServices::digitalpin_t TranslateInputPin(
			EmbeddedIOServices::digitalpin_t connectorPin);
		static EmbeddedIOServices::digitalpin_t TranslateOutputPin(
			EmbeddedIOServices::digitalpin_t connectorPin);

	public:
		E78DigitalService(
			MPC5xxx::MPC5xxxDigitalService& mcuDigitalService,
			MPC5xxx::DelphiDSIDigitalService& dsiDigitalService,
			Delphi28046304Device& delphiDevice);

		void InitPin(EmbeddedIOServices::digitalpin_t pin,
			EmbeddedIOServices::PinDirection direction) override;
		bool ReadPin(EmbeddedIOServices::digitalpin_t pin) override;
		void WritePin(EmbeddedIOServices::digitalpin_t pin, bool value) override;
		void AttachInterrupt(EmbeddedIOServices::digitalpin_t pin,
			EmbeddedIOServices::callback_t callBack) override;
		void DetachInterrupt(EmbeddedIOServices::digitalpin_t pin) override;
	};
}

#endif
