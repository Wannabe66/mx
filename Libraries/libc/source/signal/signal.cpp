// signal.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "../../include/signal.h"
#include "../../include/unistd.h"
#include <sys/syscall.h>

extern "C" sighandler_t signal(int signum, sighandler_t newhandler)
{
	return Library::SystemCall::InstallSignalHandler(signum, newhandler);
}

extern "C" int raise(int signum)
{
	return kill(getpid(), signum);
}

extern "C" int kill(pid_t pid, int signum)
{
	Library::SystemCall::SignalProcess(pid, signum);
	return errno == 0 ? 0 : -1;
}
