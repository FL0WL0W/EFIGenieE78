#include "MPC5566.h"
#include "E78AnalogService.h"

namespace E78
{

	EmbeddedIOServices::analogpin_t E78AnalogService::TranslatePin(EmbeddedIOServices::analogpin_t connectorPin)
	{
		switch (connectorPin)
		{
            case 263: return 0;
            case 265: return 1;
            case 109: return 2;
            case 359: return 3;
            case 102: return 4;
            case 110: return 5;
            case 344: return 7;
            case 347: return 17;
            case 345: return 18;
            case 112: return 19;
            case 364: return 22;
            case 357: return 23;
            case 246: return 24;
            case 362: return 26;
            case 146: return 28;
            case 346: return 30;
            case 363: return 31;
            case 145: return 32;
            case 111: return 33;
            case 261: return 34;
            case 262: return 35;
            case 356: return 36;
            case 266: return 39;
            case 267: return 64;
            case 367: return 68;
            case 268: return 72;
            case 365: return 76;
            case 358: return 80;
            case 360: return 82;
            case 361: return 84;
            case 264: return 86;
            case 249: return 88;
            default: return -1;
		}
	}

	E78AnalogService::E78AnalogService() : _mcuAnalogService(EQADC) {}

	void E78AnalogService::InitPin(EmbeddedIOServices::analogpin_t pin)
	{
		_mcuAnalogService.InitPin(TranslatePin(pin));
	}

	float E78AnalogService::ReadPin(EmbeddedIOServices::analogpin_t pin)
	{
		return _mcuAnalogService.ReadPin(TranslatePin(pin));
	}
}