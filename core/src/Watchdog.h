#pragma once
#include <windows.h>

namespace gs {
// Runs the crash guard for the core process `pid` until it exits. See Watchdog.cpp.
int RunWatchdog(DWORD pid);
}
