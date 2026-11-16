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

    // Writes the monitor count and the zone modes to the settings file, then asks the driver to reload.
    // Each virtual monitor gets every zone mode, so any zone can land on any monitor.
    // gpuName pins rendering to the GPU that drives the panel (needed so capture and scanout share one device).
    bool Configure(const std::vector<ZoneRect>& zones, int refreshHz, const std::string& gpuName);

    static std::wstring SettingsPath();

private:
    bool Send(const std::wstring& command, std::wstring* reply = nullptr) const;
};

// The driver answers some commands in ANSI and others in UTF-16LE (exposed for tests).
std::wstring DecodeVddReply(const unsigned char* data, size_t size);

// True for a monitor created by VDD. Its targets report HDMI as output technology, so the
// hardware id in the monitor device path (\\?\DISPLAY#MTT1337#...) is what identifies them.
bool IsVddMonitorPath(const std::wstring& devicePath);

// Builds the replacement text for the settings file (exposed for tests).
std::string PatchVddSettings(const std::string& original, int count, const std::vector<ZoneRect>& zones, int refreshHz,
                             const std::string& gpuName);

} // namespace gs
