// Keyboard.cpp
// Copyright (c) 2013 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include <Kernel.hpp>

namespace Kernel {
namespace HardwareAbstraction {
namespace Devices
{
	Keyboard::~Keyboard()
	{
	}

	void Keyboard::HandleKeypress()
	{
	}

	uint8_t Keyboard::ReadBuffer()
	{
		return 0;
	}

	bool Keyboard::ItemsInBuffer()
	{
		return 0;
	}
}
}
}
