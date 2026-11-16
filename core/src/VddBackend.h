#pragma once
#include "Layout.h"
#include <set>
#include <string>
#include <vector>

namespace gs {

// Controls the signed open-source Virtual Display Driver (github.com/VirtualDrivers/Virtual-Display-Driver).
// The driver reads C:\VirtualDisplayDriver\vdd_settings.xml and listens on \\.\pipe\MTTVirtualDisplayPipe.
class VddBackend {
public:
    bool IsInstalled() const;       // settings file exists and the pipe answers PING
    bool Ping() const;

    // Makes sure the driver offers kMaxZones monitors with the widths of every built-in preset and of
    // `zones`, so switching presets or refresh is a plain mode switch. Only when a width is missing does
    // it ask the driver to reload — VDD 25.7.23 (mttvdd 2.35.0) crashes on every reload and Windows
    // disables it after a few crashes, so that must stay rare (first run, a new custom width).
    // gpuName pins rendering to the GPU that drives the panel (capture and scanout share one device).
    bool Configure(const std::vector<ZoneRect>& zones, int panelW, int panelH, int refreshHz, const std::string& gpuName);

    static std::wstring SettingsPath();

private:
    bool Send(const std::wstring& command, std::wstring* reply = nullptr) const;
};

// The driver answers some commands in ANSI and others in UTF-16LE (exposed for tests).
std::wstring DecodeVddReply(const unsigned char* data, size_t size);

// True for a monitor created by VDD. Its targets report HDMI as output technology, so the
// hardware id in the monitor device path (\\?\DISPLAY#MTT1337#...) is what identifies them.
bool IsVddMonitorPath(const std::wstring& devicePath);

// The parts of vdd_settings.xml GScreen cares about (exposed for tests).
struct VddSettings {
    int count = 0;
    std::string gpu;
    std::set<std::pair<int, int>> sizes; // width, height
};
VddSettings ParseVddSettings(const std::string& xml);

// More resolutions than this and VDD 25.7.23 creates no monitors (it failed with 121).
constexpr size_t kMaxVddWidths = 16;

// Widths of all built-in presets on this panel plus the given zones.
std::set<int> RequiredWidths(int panelW, const std::vector<ZoneRect>& zones);

// True when the settings already offer kMaxZones monitors, the GPU and every width at panelH.
bool VddSettingsSatisfy(const std::string& xml, const std::set<int>& widths, int panelH, const std::string& gpuName);

// Builds the replacement text for the settings file: `widths` plus those already registered (capped).
std::string PatchVddSettings(const std::string& original, const std::set<int>& widths, int panelH, int refreshHz,
                             const std::string& gpuName);

} // namespace gs
