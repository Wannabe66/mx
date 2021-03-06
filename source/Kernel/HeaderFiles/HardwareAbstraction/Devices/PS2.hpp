// PS2.hpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under Creative Commons Attribution ShareAlike 3.0 Unported.


#include <stdint.h>
#pragma once

namespace Kernel {
namespace HardwareAbstraction {
namespace Devices
{
	class __attribute__((packed)) PS2Controller
	{
		public:
			PS2Controller();
	};

	namespace PS2
	{
		extern uint8_t Device1Buffer;
		extern uint8_t Device2Buffer;
		extern const uint8_t DataPort;
		extern const uint8_t CommandPort;

		void Initialise();
	}
}
}
}
