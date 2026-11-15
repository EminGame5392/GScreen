#pragma once
#include "Layout.h"
#include <windows.h>
#include <string>
#include <vector>

namespace gs {

struct TargetKey {
    LUID adapterId{};
    UINT32 targetId = 0;
    bool operator==(const TargetKey& o) const {
        return adapterId.LowPart == o.adapterId.LowPart && adapterId.HighPart == o.adapterId.HighPart && targetId == o.targetId;
    }
};

struct DisplayInfo {
    TargetKey key;
    bool active = false;       // part of the desktop right now
    bool isVirtual = false;    // indirect display (VDD) rather than a cable to a real panel
    std::wstring friendlyName; // from EDID, e.g. "Odyssey G95SD"
    std::wstring devicePath;   // monitor device interface path
    std::wstring gdiName;      // \\.\DISPLAYn, only for active displays
    int x = 0, y = 0, width = 0, height = 0;
    double refreshHz = 0;
    int nativeWidth = 0, nativeHeight = 0; // preferred mode from the monitor
};

// Every connected target, one entry per monitor.
std::vector<DisplayInfo> EnumerateDisplays();

// The real big panel: the widest non-virtual display (active or not).
const DisplayInfo* FindPhysicalPanel(const std::vector<DisplayInfo>& all);
std::vector<const DisplayInfo*> FindVirtualDisplays(const std::vector<DisplayInfo>& all);

// Adds the given targets to the desktop (keeps everything that is already active).
bool AttachTargets(const std::vector<TargetKey>& targets);

// Removes every virtual display from the desktop.
bool DetachVirtualDisplays();

// Places virtual displays side by side at the zone positions and resolutions; the first becomes primary.
// originX shifts the whole row (preview mode keeps the panel at 0 and puts the zones to its right).
// parkPanel (window mode): the real panel moves to the row's bottom-right corner, touching it at a single point,
// so the cursor and windows have no shared edge to cross onto it.
bool ArrangeVirtualDisplays(const std::vector<const DisplayInfo*>& virtuals, const std::vector<ZoneRect>& zones, int refreshHz,
                            int originX = 0, bool firstIsPrimary = true, const DisplayInfo* parkPanel = nullptr);

// Makes the panel primary at its native resolution and highest refresh rate.
bool MakePanelPrimary(const DisplayInfo& panel);

} // namespace gs
