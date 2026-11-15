#pragma once
#include "Compositor.h"
#include "Config.h"
#include "DisplayTopology.h"
#include "Json.h"
#include "PanelGuard.h"
#include "VddBackend.h"
#include <string>

namespace gs {

// How the composed frame reaches the panel.
enum class Presentation {
    Exclusive, // panel removed from the desktop and owned via DisplayCore (Enterprise / Pro for Workstations)
    Window,    // panel parked in a corner of the desktop, covered by a topmost window (Home / Pro)
    Preview,   // testing: composite in an ordinary window, panel untouched
};

// Owns the whole split/merge state machine. Every method runs on the main (UI message) thread.
class Engine {
public:
    bool Init();
    Json Status();
    // Applies a layout. Split: virtual monitors + compositor; merged: plain panel.
    // On any failure during split the engine rolls back to merged and reports the error.
    bool Apply(const Layout& layout, std::string& error);
    bool ApplyPreset(const std::string& id, std::string& error);
    bool ToggleMode(std::string& error);
    // Back to the plain panel no matter what state we think we are in.
    void RestoreMerged();
    // Called every 500 ms: notices a dead compositor, keeps stray windows off the parked panel.
    void Tick();
    const Config& GetConfig() const { return config_; }
    // Preview: composite goes to a window and the panel stays on the desktop. Command-line only, never saved.
    void SetPreview(bool on) { preview_ = on; }

private:
    bool EnterSplit(const Layout& layout, std::string& error);
    bool WaitForVirtualDisplays(size_t count, int timeoutMs);
    std::string PanelGpuName(const DisplayInfo& panel);
    Presentation ChoosePresentation() const;

    Config config_;
    VddBackend vdd_;
    Compositor compositor_;
    PanelGuard guard_;
    Mode mode_ = Mode::Merged;
    Presentation presentation_ = Presentation::Window;
    bool preview_ = false;
    bool licensed_ = false;
    std::string lastError_;
    std::vector<ZoneRect> zones_;
    DisplayInfo panel_; // snapshot taken while the panel was on the desktop
    bool havePanel_ = false;
};

} // namespace gs
