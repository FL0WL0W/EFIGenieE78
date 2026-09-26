#include "E78System.h"
#include "UDSService.h"
#include "ServiceRegistry.h"
#include "EngineMain.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace MPC5xxx;
using namespace EFIGenie;
using namespace EmbeddedIOServices;
using namespace EmbeddedIOOperations;
using namespace OperationArchitecture;

extern "C" [[noreturn]] void ExitToBootloaderUploadRoutine();

extern "C" __attribute__((weak)) bool WriteToFlash(
	std::uint32_t address,
	const std::uint8_t* data,
	std::size_t length)
{
	(void)address;
	(void)data;
	(void)length;
	return false;
}

ICommunicationService* isotpConsole = nullptr;
char line[16384];
std::size_t linelength = 0;
extern "C" int write(int file, const void* data, std::size_t length)
{
	if((file != 1 && file != 2) || data == nullptr || isotpConsole == nullptr)
		return -1;
	for(std::size_t i = 0; i < length; i++)
	{
		line[linelength++] = static_cast<const char*>(data)[i];
		if(static_cast<const char*>(data)[i] == '\n' || linelength >= sizeof(line))
		{
			if(isotpConsole->Ready())
			{
				isotpConsole->Send(line, linelength);
				linelength = 0;
			}
		}
	}
	return static_cast<int>(length);
}

extern uint32_t _config;
ServiceRegistry _serviceRegistry;
EngineMain *_engineMain = 0;
GeneratorMap<Variable> _variableMap;

extern "C" int main()
{
	E78::E78System system;

	const uint8_t canBusA = MPC5xxx::MPC5xxxFlexCAN2Service::Initialize(
			CAN_A,
			MPC5xxx::CANBaudRate::Kbps500,
			1U);
	isotpConsole = MPC5xxx::MPC5xxxFlexCAN2Service::Instance().GetISOTPService(
				  {0x600U, canBusA},
				  {0x608U, canBusA});
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	// Constructors configure interrupt-backed peripherals and publish their
	// service instances. Do not allow a pending peripheral interrupt inherited
	// from the bootloader to observe a partially constructed service object.
	// Timer calibration below requires interrupts, so enable them only after
	// the system, CAN transport, and diagnostic console are all ready.
	asm volatile("wrteei 1\n\tisync" ::: "memory");

	system.Initialize();
	_serviceRegistry.Register<IDigitalService>(&system.DigitalService);
	// _serviceRegistry.Register<IAnalogService>(&system.AnalogService);
	_serviceRegistry.Register<ITimerService>(&system.TimerService);
	// _serviceRegistry.Register<IPwmService>(&system.PwmService);

	ICommunicationService* const isotp = MPC5xxx::MPC5xxxFlexCAN2Service::Instance().GetISOTPService(
				  {0x7E0U, canBusA},
				  {0x7E8U, canBusA});
	const E78::UDSMemoryRegion udsReadRegions[] = {
		{0x00000000U, 0x00003FE0U, true},
		{0x00004000U, 0x0001BFE0U, true},
		{0x00020000U, 0x003E0000U, true},
		{0x00FFFC00U, 0x00000400U, true},
		{0x40000000U, 0x00040000U, false},
	};
	const E78::UDSMemoryRegion udsWriteRegions[] = {
		{reinterpret_cast<uint32_t>(&_config), 0x00020000U, true},
		{0x40000000U, 0x00040000U, false},
	};
	E78::UDSService uds(
		*isotp,
		udsReadRegions,
		sizeof(udsReadRegions) / sizeof(udsReadRegions[0]),
		udsWriteRegions,
		sizeof(udsWriteRegions) / sizeof(udsWriteRegions[0]),
		WriteToFlash,
		ExitToBootloaderUploadRoutine);

	const size_t configSize = _config + sizeof(uint32_t) + sizeof(uint32_t);
	size_t configgedSize = 0;
	_engineMain = new EngineMain(&_config, configgedSize, &_serviceRegistry, &_variableMap);
	if(configSize != configgedSize)
	{
		delete _engineMain;
		_engineMain = 0;
	}

	if(_engineMain != 0)
		_engineMain->Setup();

	const std::uint8_t alive = 0x00U | (_engineMain != 0 ? 0x01U : 0x00U);
	isotp->Send(&alive, 1U);
	while (true)
	{
        if(_engineMain != 0)
            _engineMain->Loop();
	}
}
