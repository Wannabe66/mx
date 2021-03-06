// read.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "../../include/unistd.h"
#include <sys/syscall.h>

extern "C" ssize_t read(int fd, void* buf, size_t count)
{
	return Library::SystemCall::Read(fd, (uint8_t*) buf, count);
}
