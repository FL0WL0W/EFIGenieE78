#include "E78DigitalService.h"

namespace
{
	constexpr EmbeddedIOServices::digitalpin_t UnmappedPin = 0xFFFFU;
	constexpr EmbeddedIOServices::digitalpin_t DSIPinFlag = 0x8000U;
	constexpr EmbeddedIOServices::digitalpin_t DSPIPinFlag = 0x4000U;

	constexpr EmbeddedIOServices::digitalpin_t DSIPin(
		EmbeddedIOServices::digitalpin_t pin)
	{
		return static_cast<EmbeddedIOServices::digitalpin_t>(DSIPinFlag | pin);
	}

	constexpr EmbeddedIOServices::digitalpin_t DSPIPin(
		EmbeddedIOServices::digitalpin_t pin)
	{
		return static_cast<EmbeddedIOServices::digitalpin_t>(DSPIPinFlag | pin);
	}
}

namespace E78
{
	E78DigitalService::E78DigitalService(
		MPC5xxx::MPC5xxxDigitalService& mcuDigitalService,
		MPC5xxx::DelphiDSIDigitalService& dsiDigitalService,
		Delphi28046304Device& delphiDevice)
		: _mcuDigitalService(mcuDigitalService),
		  _dsiDigitalService(dsiDigitalService),
		  _delphiDevice(delphiDevice)
	{
	}

	EmbeddedIOServices::digitalpin_t E78DigitalService::TranslateInputPin(
		EmbeddedIOServices::digitalpin_t connectorPin)
	{
		switch (connectorPin)
		{
		case 103U: return 155U; // X1-3  Mass air-flow signal
		case 104U: return 115U; // X1-4
		case 105U: return 117U; // X1-5
		case 106U: return 92U;  // X1-6
		case 107U: return 175U; // X1-7
		case 108U: return 163U; // X1-8  Humidity sensor signal
		case 118U: return 118U; // X1-18 Accessory wakeup serial data
		case 133U: return 203U; // X1-33 Brake signal
		case 134U: return 204U; // X1-34 Park/neutral signal
		case 144U: return 178U; // X1-44
		case 147U: return 190U; // X1-47 Charge-indicator feedback

		case 231U: return 116U; // X2-31 Oil-pressure switch
		case 245U: return 177U; // X2-45 SENT1 signal
		case 256U: return 146U; // X2-56 Crankshaft signal
		case 257U: return 149U; // X2-57 Exhaust camshaft signal
		case 259U: return 151U; // X2-59
		case 260U: return 148U; // X2-60 Intake camshaft signal
		case 269U: return 181U; // X2-69 Knock sensor signal
		case 301U: return 121U; // X3-1  Reverse switch
		case 302U: return 123U; // X3-2
		case 318U: return 130U; // X3-18
		case 333U: return 154U; // X3-33
		case 334U: return 157U; // X3-34
		case 348U: return 165U; // X3-48 Vehicle-speed signal
		case 353U: return 152U; // X3-53
		case 354U: return 153U; // X3-54
		case 355U: return 164U; // X3-55 Vehicle-speed signal
		default: return UnmappedPin;
		}
	}

	EmbeddedIOServices::digitalpin_t E78DigitalService::TranslateOutputPin(
		EmbeddedIOServices::digitalpin_t connectorPin)
	{
		// The connector number is the hundreds digit: Xn-m => n * 100 + m.
		switch (connectorPin)
		{
		case 114U: return 191U; // X1-14 Secondary fuel-pump relay
		case 127U: return 143U; // X1-27 Starter-enable relay
		case 128U: return 197U; // X1-28 Primary fuel-pump relay
		case 140U: return 180U; // X1-40 Powertrain relay
		case 141U: return 185U; // X1-41 Cooling-fan relay
		case 147U: return 196U; // X1-47 Charge-indicator control
		case 150U: return 166U; // X1-50
		case 151U: return DSPIPin(10U); // X1-51 Delphi output 10
		case 152U: return 159U; // X1-52 Check-engine indicator
		case 153U: return 114U; // X1-53 A/C compressor clutch relay
		case 154U: return DSPIPin(9U);  // X1-54 High-speed fan relay
		case 155U: return 199U; // X1-55
		case 156U: return DSPIPin(11U); // X1-56 EVAP canister vent

		case 201U: return 167U; // X2-1  Ignition control
		case 202U: return 132U; // X2-2  Injector control
		case 203U: return 133U; // X2-3  Injector control
		case 204U: return 134U; // X2-4  Injector control
		case 205U: return 135U; // X2-5  Injector control
		case 206U: return 136U; // X2-6  Injector control
		case 207U: return 137U; // X2-7  Injector control
		case 208U: return 138U; // X2-8  Injector control
		case 209U: return 139U; // X2-9  Injector control
		case 210U: return DSIPin(12U); // X2-10 Cam phaser exhaust
		case 211U: return DSIPin(13U); // X2-11 Cam phaser intake
		case 212U: return DSIPin(14U); // X2-12
		case 214U: return DSIPin(6U);  // X2-14 Thermostat heater
		case 215U: return 187U; // X2-15 ETC open
		case 216U: return 188U; // X2-16 ETC close
		case 217U: return 168U; // X2-17 Ignition control
		case 218U: return 173U; // X2-18 Ignition control
		case 232U: return DSIPin(15U); // X2-32
		case 233U: return 169U; // X2-33 Ignition control
		case 234U: return 172U; // X2-34 Ignition control
		case 252U: return DSIPin(1U); // X2-52 O2 heater
		case 253U: return 170U; // X2-53 Ignition control
		case 254U: return 171U; // X2-54 Ignition control
		case 255U: return 174U; // X2-55 Ignition control
		case 272U: return DSIPin(0U); // X2-72 O2 heater

		case 303U: return DSIPin(19U); // X3-3
		case 304U: return DSIPin(17U); // X3-4
		case 305U: return DSIPin(18U); // X3-5
		case 306U: return DSIPin(16U); // X3-6
		case 307U: return 101U; // X3-7
		case 308U: return DSIPin(10U); // X3-8
		case 309U: return DSIPin(11U); // X3-9
		case 310U: return DSIPin(8U);  // X3-10 EVAP purge output
		case 311U: return DSIPin(4U);  // X3-11
		case 312U: return DSIPin(9U);  // X3-12 Intake manifold valve
		case 313U: return DSIPin(7U);  // X3-13 Turbo bypass output
		case 314U: return DSIPin(20U); // X3-14
		case 315U: return DSIPin(5U);  // X3-15 Wastegate output
		case 316U: return DSIPin(3U);  // X3-16 tentative O2 heater output
		case 317U: return 156U; // X3-17 Generator field duty cycle
		case 332U: return DSIPin(2U); // X3-32 O2 heater output
		default: return UnmappedPin;
		}
	}

	void E78DigitalService::InitPin(
		EmbeddedIOServices::digitalpin_t pin,
		EmbeddedIOServices::PinDirection direction)
	{
		const EmbeddedIOServices::digitalpin_t translated =
			direction == EmbeddedIOServices::Out
				? TranslateOutputPin(pin)
				: TranslateInputPin(pin);
		if (translated == UnmappedPin) return;
		if ((translated & DSIPinFlag) != 0U)
			_dsiDigitalService.InitPin(translated & ~DSIPinFlag, direction);
		else if ((translated & DSPIPinFlag) != 0U)
			return;
		else
			_mcuDigitalService.InitPin(translated, direction);
	}

	bool E78DigitalService::ReadPin(EmbeddedIOServices::digitalpin_t pin)
	{
		EmbeddedIOServices::digitalpin_t translated = TranslateInputPin(pin);
		if (translated == UnmappedPin)
			translated = TranslateOutputPin(pin);
		if (translated == UnmappedPin) return false;
		if ((translated & DSIPinFlag) != 0U)
			return _dsiDigitalService.ReadPin(translated & ~DSIPinFlag);
		if ((translated & DSPIPinFlag) != 0U)
			return _delphiDevice.ReadDiscreteOutput(
				static_cast<std::uint8_t>(translated & ~DSPIPinFlag));
		return _mcuDigitalService.ReadPin(translated);
	}

	void E78DigitalService::WritePin(
		EmbeddedIOServices::digitalpin_t pin,
		bool value)
	{
		const EmbeddedIOServices::digitalpin_t translated = TranslateOutputPin(pin);
		if (translated == UnmappedPin) return;
		if ((translated & DSIPinFlag) != 0U)
			_dsiDigitalService.WritePin(translated & ~DSIPinFlag, value);
		else if ((translated & DSPIPinFlag) != 0U)
			_delphiDevice.WriteDiscreteOutput(
				static_cast<std::uint8_t>(translated & ~DSPIPinFlag), value);
		else
			_mcuDigitalService.WritePin(translated, value);
	}

	void E78DigitalService::AttachInterrupt(
		EmbeddedIOServices::digitalpin_t pin,
		EmbeddedIOServices::callback_t callBack)
	{
		const EmbeddedIOServices::digitalpin_t translated = TranslateInputPin(pin);
		if (translated == UnmappedPin) return;
		_mcuDigitalService.AttachInterrupt(translated, callBack);
	}

	void E78DigitalService::DetachInterrupt(
		EmbeddedIOServices::digitalpin_t pin)
	{
		const EmbeddedIOServices::digitalpin_t translated = TranslateInputPin(pin);
		if (translated == UnmappedPin) return;
		_mcuDigitalService.DetachInterrupt(translated);
	}
}
