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


// // X1 Output
// 114U, 127U, 128U, 140U, 141U, 147U, 150U,
// 151U, 152U, 153U, 154U, 155U, 156U,
// // X2 Output
// 201U, 202U, 203U, 204U, 205U, 206U, 207U, 
// 208U, 209U, 210U, 211U, 212U, 214U, 215U,
// 216U, 217U, 218U, 232U, 233U, 234U, 252U,
// 253U, 254U, 255U, 272U,
// // X3 Output
// 303U, 304U, 305U, 306U, 307U, 308U, 309U,
// 310U, 311U, 312U, 313U, 314U, 315U, 316U,
// 317U, 332U

constexpr std::uint32_t LoopPeriodTimebaseTicks = 384000;
void ServiceCoreWatchdog()
{
	const std::uint32_t watchdogService = 0x40000000U;
	asm volatile(
		"mtspr 336, %0\n"
		:
		: "r"(watchdogService)
		: "memory");
}

extern uint32_t _config;
ServiceRegistry _serviceRegistry;
EngineMain *_engineMain = 0;
GeneratorMap<Variable> _variableMap;

extern "C" int main()
{
	asm("wrteei 1");

	E78::E78System system;
	system.Initialize();
	_serviceRegistry.Register<IDigitalService>(&system.DigitalService);
	// _serviceRegistry.Register<IAnalogService>(&system.AnalogService);
	_serviceRegistry.Register<ITimerService>(&system.TimerService);
	// _serviceRegistry.Register<IPwmService>(&system.PwmService);

	const uint8_t canBusA = MPC5xxx::MPC5xxxFlexCAN2Service::Initialize(
			CAN_A,
			MPC5xxx::CANBaudRate::Kbps500);
	ICommunicationService* const isotp = MPC5xxx::MPC5xxxFlexCAN2Service::Instance().GetISOTPService(
				  {0x7E0U, canBusA},
				  {0x7E8U, canBusA});
	const E78::UDSMemoryRegion udsReadRegions[] = {
		{0x00000000U, 0x00003FE0U, true},
		{0x00004000U, 0x0001BFE0U, true},
		{0x00020000U, 0x003E0000U, true},
		{0x40000000U, 0x00040000U, false},
	};
	const E78::UDSMemoryRegion udsWriteRegions[] = {
		{0x00000000U, 0x00400000U, true},
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
	isotpConsole = MPC5xxx::MPC5xxxFlexCAN2Service::Instance().GetISOTPService(
				  {0x600U, canBusA},
				  {0x608U, canBusA});
	std::setvbuf(stdout, nullptr, _IONBF, 0);

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
	std::uint32_t loopStart = system.TimerService.GetTick();
	while (true)
	{
		system.Service();
        if(_engineMain != 0)
            _engineMain->Loop();

		const std::uint32_t now = system.TimerService.GetTick();
		if (static_cast<std::uint32_t>(now - loopStart) <
			LoopPeriodTimebaseTicks)
			continue;

		loopStart = now;
		ServiceCoreWatchdog();
		system.ServiceWatchdogs();
	}
}
