#pragma once
#include "DisplayTopology.h"

namespace gs {

// Windows "Remove display from desktop" (monitor specialization).
// A specialized monitor leaves the desktop and one app can own it through Windows.Devices.Display.Core.
// Owning it is licensed only on Windows 11 Enterprise, Pro for Workstations and IoT Enterprise:
// on Home/Pro the license value Display-Specialized-Displays-Enabled is 0 and acquisition fails.
bool ExclusiveModeLicensed();
bool SetPanelRemovedFromDesktop(const DisplayInfo& panel, bool removed);
bool IsPanelRemovedFromDesktop(const DisplayInfo& panel);

} // namespace gs
