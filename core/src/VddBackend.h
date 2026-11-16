#pragma once
#include "Layout.h"
#include <string>
#include <vector>

namespace gs {

// Controls the signed open-source Virtual Display Driver (github.com/VirtualDrivers/Virtual-Display-Driver).
// The driver reads C:\VirtualDisplayDriver\vdd_settings.xml and listens on \\.\pipe\MTTVirtualDisplayPipe.
class VddBackend {
public:
    bool IsInstalled() const;       // settings file exists and the pipe answers PING
    bool Ping() const;

    // Makes sure the driver offers kMaxZones monitors with every zone width ComputeZones can produce for
    // this panel, so changing the layout or refresh is a plain mode switch. Only when the settings file
    // has to change does it ask the driver to reload — VDD 25.7.23 (mttvdd 2.35.0) crashes on every
    // reload and Windows disables it after a few crashes, so that must stay rare (first run, new panel).
    // gpuName pins rendering to the GPU that drives the panel (capture and scanout share one device).
    bool Configure(int panelW, int panelH, int refreshHz, const std::string& gpuName);

    static std::wstring SettingsPath();

private:
    bool Send(const std::wstring& command, std::wstring* reply = nullptr) const;
};

// The driver answers some commands in ANSI and others in UTF-16LE (exposed for tests).
std::wstring DecodeVddReply(const unsigned char* data, size_t size);

// True for a monitor created by VDD. Its targets report HDMI as output technology, so the
// hardware id in the monitor device path (\\?\DISPLAY#MTT1337#...) is what identifies them.
bool IsVddMonitorPath(const std::wstring& devicePath);

// Every zone width ComputeZones can return for a panel this wide, ascending (exposed for tests).
std::vector<int> ZoneWidthGrid(int panelW);

// Builds the replacement text for the settings file (exposed for tests).
std::string PatchVddSettings(const std::string& original, int panelW, int panelH, int refreshHz, const std::string& gpuName);

} // namespace gs
