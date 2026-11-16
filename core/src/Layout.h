#pragma once
#include "Json.h"
#include <string>
#include <vector>

namespace gs {

enum class Mode { Split, Merged };

struct ZoneRect {
    int x = 0, y = 0, width = 0, height = 0;
};

struct Preset {
    std::string id;
    std::string name;
    std::vector<double> ratios; // 2 or 3 entries, relative widths
};

struct Layout {
    Mode mode = Mode::Merged;
    std::vector<double> ratios{1, 1, 1};
    std::string presetId = "three-equal";
    int refreshHz = 240;   // requested refresh of virtual monitors
    bool showDividers = false; // off by default: static lines burn into OLED panels
};

constexpr int kMinZoneWidth = 640;
// 32 keeps the driver's mode list short (every possible zone width is pre-registered with VDD).
constexpr int kWidthAlign = 32;
constexpr int kMaxZones = 3;

// Splits a physical panel of panelW x panelH into side-by-side zones.
// Widths are multiples of kWidthAlign, each >= kMinZoneWidth, and sum exactly to panelW
// (the last zone absorbs rounding). Ratios are clamped/normalized; invalid input → one zone.
std::vector<ZoneRect> ComputeZones(int panelW, int panelH, const std::vector<double>& ratios);

std::vector<Preset> DefaultPresets();

Json LayoutToJson(const Layout& l);
Layout LayoutFromJson(const Json& j);
const char* ModeName(Mode m);

} // namespace gs
