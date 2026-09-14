#include "DelphiDSIDigitalService.h"

namespace
{
	constexpr std::uint32_t kHalt = 0x00000001U;
	constexpr std::uint32_t kPCS0 = 0x00010000U;
	constexpr EmbeddedIOServices::digitalpin_t kOutputCount = 21U;

	std::uint32_t DisableExternalInterrupts()
	{
		std::uint32_t machineState;
		asm volatile(
			"mfmsr %0\n"
			"wrteei 0\n"
			"isync\n"
			: "=r"(machineState)
			:
			: "memory");
		return machineState;
	}

	void RestoreExternalInterrupts(const std::uint32_t machineState)
	{
		if ((machineState & 0x00008000U) != 0U)
			asm volatile("wrteei 1\n\tisync" ::: "memory");
	}
}

namespace MPC5xxx
{
	DelphiDSIDigitalService::DelphiDSIDigitalService(
		volatile DSPI_tag* leadingModule,
		volatile DSPI_tag* clockingModule,
		std::uint32_t leadingModuleConfiguration,
		std::uint32_t clockingModuleConfiguration,
		std::uint32_t leadingClockTransferAttributes,
		std::uint32_t clockingClockTransferAttributes,
		std::uint32_t serialConfiguration,
		std::uint32_t outputMask,
		std::uint32_t initialValue)
		: _leadingModule(leadingModule),
		  _clockingModule(clockingModule),
		  _outputMask(outputMask)
	{
		// E78 routing: DSPI-C supplies SCK/PCS0, and the chained 21-bit
		// stream exits through SOUTA after DSPI-A's five leading bits.
		SIU.DISR.R = 0xA8000100U;

		_leadingModule->MCR.R = leadingModuleConfiguration | kHalt;
		_clockingModule->MCR.R = clockingModuleConfiguration | kHalt;
		_leadingModule->TCR.R = 0U;
		_clockingModule->TCR.R = 0U;
		_leadingModule->RSER.R = 0U;
		_clockingModule->RSER.R = 0U;
		_leadingModule->CTAR[1].R = leadingClockTransferAttributes;
		_clockingModule->CTAR[0].R = clockingClockTransferAttributes;
		_leadingModule->DSICR.R = serialConfiguration;
		_clockingModule->DSICR.R = serialConfiguration;
		_leadingModule->PUSHR.R = kPCS0;
		_clockingModule->PUSHR.R = kPCS0;
		Set(initialValue);
	}

	void DelphiDSIDigitalService::Start()
	{
		if (_started)
			return;
		// Match stock order: release the DSPI-A chained slave first, followed
		// by the DSPI-C clocking master that initiates the continuous stream.
		_leadingModule->MCR.R &= ~kHalt;
		_clockingModule->MCR.R &= ~kHalt;
		_started = true;
	}

	void DelphiDSIDigitalService::Set(std::uint32_t value)
	{
		// The 21-bit stream spans two DSPI modules. Commit the shadow and both
		// ASDR halves as one short operation so an interrupting writer cannot
		// leave the hardware with halves from different values.
		const std::uint32_t machineState = DisableExternalInterrupts();
		_value = value & _outputMask;
		_leadingModule->ASDR.R = (_value >> 16U) & 0x001FU;
		_clockingModule->ASDR.R = _value & 0xFFFFU;
		RestoreExternalInterrupts(machineState);
	}

	void DelphiDSIDigitalService::InitPin(
		EmbeddedIOServices::digitalpin_t pin,
		EmbeddedIOServices::PinDirection direction)
	{
		(void)pin;
		(void)direction;
	}

	bool DelphiDSIDigitalService::ReadPin(
		EmbeddedIOServices::digitalpin_t pin)
	{
		return pin < kOutputCount && (_value & (1UL << pin)) != 0U;
	}

	void DelphiDSIDigitalService::WritePin(
		EmbeddedIOServices::digitalpin_t pin,
		bool value)
	{
		if (pin >= kOutputCount)
			return;
		const std::uint32_t bit = 1UL << pin;
		const std::uint32_t machineState = DisableExternalInterrupts();
		_value = (value ? _value | bit : _value & ~bit) & _outputMask;
		_leadingModule->ASDR.R = (_value >> 16U) & 0x001FU;
		_clockingModule->ASDR.R = _value & 0xFFFFU;
		RestoreExternalInterrupts(machineState);
	}

	void DelphiDSIDigitalService::AttachInterrupt(
		EmbeddedIOServices::digitalpin_t pin,
		EmbeddedIOServices::callback_t callBack)
	{
		(void)pin;
		(void)callBack;
	}

	void DelphiDSIDigitalService::DetachInterrupt(
		EmbeddedIOServices::digitalpin_t pin)
	{
		(void)pin;
	}
}
