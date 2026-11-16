#include "DisplayTopology.h"
#include "VddBackend.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace gs {

namespace {

bool IsVirtualTech(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY t) {
    return t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED;
}

bool Query(UINT32 flags, std::vector<DISPLAYCONFIG_PATH_INFO>& paths, std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        UINT32 np = 0, nm = 0;
        if (GetDisplayConfigBufferSizes(flags, &np, &nm) != ERROR_SUCCESS) return false;
        paths.resize(np);
        modes.resize(nm);
        LONG r = QueryDisplayConfig(flags, &np, paths.data(), &nm, modes.data(), nullptr);
        if (r == ERROR_INSUFFICIENT_BUFFER) continue; // topology changed between the two calls
        if (r != ERROR_SUCCESS) return false;
        paths.resize(np);
        modes.resize(nm);
        return true;
    }
    return false;
}

void FillTargetName(DisplayInfo& d) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME tn{};
    tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    tn.header.size = sizeof tn;
    tn.header.adapterId = d.key.adapterId;
    tn.header.id = d.key.targetId;
    if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS) {
        d.friendlyName = tn.monitorFriendlyDeviceName;
        d.devicePath = tn.monitorDevicePath;
        d.isVirtual = IsVirtualTech(tn.outputTechnology) || IsVddMonitorPath(d.devicePath);
    }
    DISPLAYCONFIG_TARGET_PREFERRED_MODE pm{};
    pm.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE;
    pm.header.size = sizeof pm;
    pm.header.adapterId = d.key.adapterId;
    pm.header.id = d.key.targetId;
    if (DisplayConfigGetDeviceInfo(&pm.header) == ERROR_SUCCESS) {
        d.nativeWidth = (int)pm.width;
        d.nativeHeight = (int)pm.height;
    }
}

std::wstring GdiNameOf(const DISPLAYCONFIG_PATH_INFO& p) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sn{};
    sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sn.header.size = sizeof sn;
    sn.header.adapterId = p.sourceInfo.adapterId;
    sn.header.id = p.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&sn.header) == ERROR_SUCCESS) return sn.viewGdiDeviceName;
    return {};
}

bool SameLuid(const LUID& a, const LUID& b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }

LONG Apply(std::vector<DISPLAYCONFIG_PATH_INFO>& paths, std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    return SetDisplayConfig((UINT32)paths.size(), paths.data(), (UINT32)modes.size(), modes.empty() ? nullptr : modes.data(),
                            SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES | SDC_SAVE_TO_DATABASE);
}

} // namespace

std::vector<DisplayInfo> EnumerateDisplays() {
    std::vector<DisplayInfo> out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (!Query(QDC_ALL_PATHS, paths, modes)) {
        LogError("QueryDisplayConfig failed: {}", GetLastError());
        return out;
    }
    // Active paths first so an active monitor is recorded with its live data.
    std::stable_partition(paths.begin(), paths.end(), [](auto& p) { return (p.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0; });
    for (auto& p : paths) {
        if (!p.targetInfo.targetAvailable) continue;
        TargetKey key{p.targetInfo.adapterId, p.targetInfo.id};
        if (std::any_of(out.begin(), out.end(), [&](auto& d) { return d.key == key; })) continue;
        DisplayInfo d;
        d.key = key;
        d.active = (p.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
        FillTargetName(d);
        if (d.active) {
            d.gdiName = GdiNameOf(p);
            UINT32 si = p.sourceInfo.modeInfoIdx; // not QDC_VIRTUAL_MODE_AWARE → plain index
            if (si != DISPLAYCONFIG_PATH_MODE_IDX_INVALID && si < modes.size() &&
                modes[si].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
                auto& sm = modes[si].sourceMode;
                d.x = sm.position.x;
                d.y = sm.position.y;
                d.width = (int)sm.width;
                d.height = (int)sm.height;
            }
            if (p.targetInfo.refreshRate.Denominator)
                d.refreshHz = (double)p.targetInfo.refreshRate.Numerator / p.targetInfo.refreshRate.Denominator;
        }
        out.push_back(std::move(d));
    }
    return out;
}

const DisplayInfo* FindPhysicalPanel(const std::vector<DisplayInfo>& all) {
    const DisplayInfo* best = nullptr;
    for (auto& d : all) {
        if (d.isVirtual) continue;
        int w = d.nativeWidth ? d.nativeWidth : d.width;
        int bw = best ? (best->nativeWidth ? best->nativeWidth : best->width) : -1;
        if (w > bw) best = &d;
    }
    return best;
}

std::vector<const DisplayInfo*> FindVirtualDisplays(const std::vector<DisplayInfo>& all) {
    std::vector<const DisplayInfo*> v;
    for (auto& d : all)
        if (d.isVirtual) v.push_back(&d);
    // Stable order: by target id, so zone A is always the same virtual monitor.
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->key.targetId < b->key.targetId; });
    return v;
}

bool AttachTargets(const std::vector<TargetKey>& targets) {
    std::vector<DISPLAYCONFIG_PATH_INFO> all;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (!Query(QDC_ALL_PATHS, all, modes)) return false;

    std::vector<DISPLAYCONFIG_PATH_INFO> chosen;
    std::set<std::pair<LONGLONG, UINT32>> usedSources; // (adapter, source id)
    auto luidKey = [](const LUID& l) { return ((LONGLONG)l.HighPart << 32) | l.LowPart; };
    std::vector<TargetKey> covered;

    for (auto& p : all) {
        if (!(p.flags & DISPLAYCONFIG_PATH_ACTIVE)) continue;
        chosen.push_back(p);
        usedSources.insert({luidKey(p.sourceInfo.adapterId), p.sourceInfo.id});
        covered.push_back({p.targetInfo.adapterId, p.targetInfo.id});
    }
    for (auto& t : targets) {
        if (std::find(covered.begin(), covered.end(), t) != covered.end()) continue;
        for (auto& p : all) {
            if (!p.targetInfo.targetAvailable) continue;
            if (!(TargetKey{p.targetInfo.adapterId, p.targetInfo.id} == t)) continue;
            auto src = std::make_pair(luidKey(p.sourceInfo.adapterId), p.sourceInfo.id);
            if (usedSources.count(src)) continue;
            DISPLAYCONFIG_PATH_INFO np = p;
            np.flags = DISPLAYCONFIG_PATH_ACTIVE;
            np.sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
            np.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
            chosen.push_back(np);
            usedSources.insert(src);
            covered.push_back(t);
            break;
        }
    }
    LONG r = Apply(chosen, modes);
    if (r != ERROR_SUCCESS) LogError("AttachTargets: SetDisplayConfig = {}", r);
    return r == ERROR_SUCCESS;
}

bool DetachVirtualDisplays() {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (!Query(QDC_ONLY_ACTIVE_PATHS, paths, modes)) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> keep;
    for (auto& p : paths) {
        DisplayInfo d;
        d.key = {p.targetInfo.adapterId, p.targetInfo.id};
        FillTargetName(d);
        if (!d.isVirtual) keep.push_back(p);
    }
    if (keep.size() == paths.size()) return true;
    if (keep.empty()) {
        LogWarn("DetachVirtualDisplays: no physical display is active; refusing to leave the desktop empty");
        return false;
    }
    LONG r = Apply(keep, modes);
    if (r != ERROR_SUCCESS) LogError("DetachVirtualDisplays: SetDisplayConfig = {}", r);
    return r == ERROR_SUCCESS;
}

static bool StageMode(const std::wstring& gdiName, int x, int y, int w, int h, int hz, bool primary) {
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    dm.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    dm.dmPosition = {x, y};
    dm.dmPelsWidth = w;
    dm.dmPelsHeight = h;
    if (hz > 0) {
        dm.dmFields |= DM_DISPLAYFREQUENCY;
        dm.dmDisplayFrequency = hz;
    }
    DWORD flags = CDS_UPDATEREGISTRY | CDS_NORESET | (primary ? CDS_SET_PRIMARY : 0);
    LONG r = ChangeDisplaySettingsExW(gdiName.c_str(), &dm, nullptr, flags, nullptr);
    if (r != DISP_CHANGE_SUCCESSFUL && hz > 0) {
        // The driver may not offer this exact refresh; let Windows pick.
        dm.dmFields &= ~DM_DISPLAYFREQUENCY;
        r = ChangeDisplaySettingsExW(gdiName.c_str(), &dm, nullptr, flags, nullptr);
    }
    if (r != DISP_CHANGE_SUCCESSFUL) LogError("ChangeDisplaySettingsEx({}) {}x{}@{} = {}", Narrow(gdiName), w, h, hz, r);
    return r == DISP_CHANGE_SUCCESSFUL;
}

bool ArrangeVirtualDisplays(const std::vector<const DisplayInfo*>& virtuals, const std::vector<ZoneRect>& zones, int refreshHz,
                            int originX, bool firstIsPrimary, const DisplayInfo* parkPanel) {
    if (virtuals.size() < zones.size()) {
        LogError("ArrangeVirtualDisplays: {} zones but only {} virtual displays", zones.size(), virtuals.size());
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < zones.size(); ++i) {
        if (virtuals[i]->gdiName.empty()) { ok = false; continue; }
        ok &= StageMode(virtuals[i]->gdiName, originX + zones[i].x, zones[i].y, zones[i].width, zones[i].height, refreshHz,
                        firstIsPrimary && i == 0);
    }
    if (parkPanel && !parkPanel->gdiName.empty() && !zones.empty()) {
        int rowRight = originX + zones.back().x + zones.back().width;
        ok &= StageMode(parkPanel->gdiName, rowRight, zones[0].height, parkPanel->nativeWidth, parkPanel->nativeHeight,
                        (int)std::lround(parkPanel->refreshHz), false);
    }
    LONG r = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    return ok && r == DISP_CHANGE_SUCCESSFUL;
}

bool MakePanelPrimary(const DisplayInfo& panel) {
    if (panel.gdiName.empty()) return false;
    // Highest refresh offered at the native resolution.
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    int bestHz = 0;
    for (DWORD i = 0; EnumDisplaySettingsW(panel.gdiName.c_str(), i, &dm); ++i)
        if ((int)dm.dmPelsWidth == panel.nativeWidth && (int)dm.dmPelsHeight == panel.nativeHeight)
            bestHz = std::max(bestHz, (int)dm.dmDisplayFrequency);
    bool ok = StageMode(panel.gdiName, 0, 0, panel.nativeWidth, panel.nativeHeight, bestHz, true);
    LONG r = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    return ok && r == DISP_CHANGE_SUCCESSFUL;
}

} // namespace gs
