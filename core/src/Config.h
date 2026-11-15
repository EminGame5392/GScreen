#pragma once
#include "Layout.h"

namespace gs {

// Persisted to %APPDATA%\GScreen\config.json.
struct Config {
    Layout layout;
    bool presentInWindow = false; // debug: show the composite in a window instead of owning the panel
};

Config LoadConfig();
void SaveConfig(const Config& c);

} // namespace gs
