#include "Engine.h"
#include "DisplayCorePresenter.h"
#include "Log.h"
#include "Specialization.h"
#include "WindowPresenter.h"
#include <chrono>
#include <cmath>
#include <thread>

namespace gs {

using winrt::com_ptr;

static void PumpFor(int ms) {
    // Display changes broadcast messages to every top-level window; keep the loop alive while we wait.
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static const char* PresentationName(Presentation p) {
    switch (p) {
    case Presentation::Exclusive: return "exclusive";
    case Presentation::Window: return "window";
    default: return "preview";
    }
}

bool Engine::Init() {
    config_ = LoadConfig();
    licensed_ = ExclusiveModeLicensed();
    LogInfo("Exclusive panel ownership licensed: {}", licensed_);
    auto all = EnumerateDisplays();
    if (auto p = FindPhysicalPanel(all)) {
        panel_ = *p;
        havePanel_ = true;
        LogInfo("Panel: {} {}x{} @ {:.0f} Hz", Narrow(panel_.friendlyName), panel_.nativeWidth, panel_.nativeHeight, panel_.refreshHz);
    } else {
        LogError("No physical display found");
    }
    // Left over from a crash (panel removed or parked, virtual monitors attached)? Start clean.
    // Inactive VDD monitors are normal (the driver keeps them around); only attached ones are leftovers.
    bool virtualsAttached = false;
    for (auto* d : FindVirtualDisplays(all)) virtualsAttached |= d->active;
    if (virtualsAttached || (havePanel_ && (IsPanelRemovedFromDesktop(panel_) || panel_.x != 0 || panel_.y != 0))) {
        LogWarn("Found a leftover split state; restoring");
        RestoreMerged();
    }
    return havePanel_;
}

Presentation Engine::ChoosePresentation() const {
    if (preview_) return Presentation::Preview;
    return licensed_ ? Presentation::Exclusive : Presentation::Window;
}

std::string Engine::PanelGpuName(const DisplayInfo& panel) {
    com_ptr<IDXGIFactory4> f;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(f.put())))) return {};
    com_ptr<IDXGIAdapter1> a;
    if (FAILED(f->EnumAdapterByLuid(panel.key.adapterId, IID_PPV_ARGS(a.put())))) return {};
    DXGI_ADAPTER_DESC1 d{};
    a->GetDesc1(&d);
    return Narrow(d.Description);
}

// VDD reloads its adapter asynchronously after SETDISPLAYCOUNT: the old monitors vanish and the
// new ones appear a moment later. Wait until the wanted count has been present (same targets)
// for stableMs, so we never grab monitors that are about to disappear.
bool Engine::WaitForVirtualDisplays(size_t count, int timeoutMs, int stableMs) {
    using clock = std::chrono::steady_clock;
    auto end = clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<TargetKey> seen;
    auto since = clock::now();
    while (clock::now() < end) {
        auto all = EnumerateDisplays();
        auto v = FindVirtualDisplays(all);
        std::vector<TargetKey> now;
        for (auto* d : v) now.push_back(d->key);
        if (now != seen) { seen = now; since = clock::now(); }
        if (seen.size() >= count && clock::now() - since >= std::chrono::milliseconds(stableMs)) return true;
        PumpFor(250);
    }
    LogWarn("Virtual displays: wanted {}, have {}", count, seen.size());
    return false;
}

bool Engine::AttachVirtualDisplays(size_t count) {
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (!WaitForVirtualDisplays(count, attempt == 1 ? 15000 : 8000, 1500)) return false;
        auto virtuals = FindVirtualDisplays(EnumerateDisplays());
        if (virtuals.size() < count) continue;
        std::vector<TargetKey> keys;
        for (size_t i = 0; i < count; ++i) keys.push_back(virtuals[i]->key);
        if (!AttachTargets(keys)) LogWarn("AttachTargets failed (attempt {})", attempt);
        PumpFor(700);
        size_t active = 0;
        for (auto* d : FindVirtualDisplays(EnumerateDisplays())) active += d->active ? 1 : 0;
        if (active >= count) return true;
        LogWarn("Attach attempt {}: {} of {} virtual displays active", attempt, active, count);
    }
    return false;
}

bool Engine::EnterSplit(const Layout& layout, std::string& error) {
    if (!havePanel_) { error = "Не найден физический монитор"; return false; }
    if (!vdd_.IsInstalled()) {
        error = "Драйвер Virtual Display Driver не установлен или не отвечает. Если Windows остановила его после сбоя: "
                "Диспетчер устройств → Видеоадаптеры → Virtual Display Driver → Отключить, затем Включить";
        return false;
    }
    presentation_ = ChoosePresentation();
    LogInfo("Presentation: {}", PresentationName(presentation_));

    const int W = panel_.nativeWidth ? panel_.nativeWidth : panel_.width;
    const int H = panel_.nativeHeight ? panel_.nativeHeight : panel_.height;
    zones_ = ComputeZones(W, H, layout.ratios);
    // Virtual monitors can't usefully run faster than the panel shows them.
    int hz = std::min(layout.refreshHz, std::max(60, (int)std::lround(panel_.refreshHz)));

    // 1. Virtual monitors with the zone resolutions.
    const int panelHz = std::max(60, (int)std::lround(panel_.refreshHz));
    if (!vdd_.Configure(zones_, W, H, panelHz, PanelGpuName(panel_))) { error = "Не удалось настроить драйвер VDD"; return false; }
    if (!AttachVirtualDisplays(zones_.size())) { error = "Windows не подключила виртуальные мониторы"; return false; }

    auto all = EnumerateDisplays();
    auto virtuals = FindVirtualDisplays(all);
    virtuals.resize(std::min(virtuals.size(), zones_.size()));
    const DisplayInfo* livePanel = FindPhysicalPanel(all);
    bool ok = false;
    switch (presentation_) {
    case Presentation::Preview: // panel stays primary at 0,0; zones to its right
        ok = ArrangeVirtualDisplays(virtuals, zones_, hz, W, false);
        break;
    case Presentation::Window: // zones at 0,0 (A primary); panel parked at the row's bottom-right corner
        ok = ArrangeVirtualDisplays(virtuals, zones_, hz, 0, true, livePanel);
        break;
    case Presentation::Exclusive: // zones at 0,0; the panel leaves the desktop below
        ok = ArrangeVirtualDisplays(virtuals, zones_, hz, 0, true);
        break;
    }
    if (!ok) { error = "Не удалось задать разрешения виртуальных мониторов"; return false; }
    PumpFor(500);

    all = EnumerateDisplays();
    virtuals = FindVirtualDisplays(all);
    std::vector<std::wstring> names;
    for (size_t i = 0; i < zones_.size() && i < virtuals.size(); ++i) names.push_back(virtuals[i]->gdiName);
    if (names.size() != zones_.size() || std::any_of(names.begin(), names.end(), [](auto& n) { return n.empty(); })) {
        error = "Часть виртуальных мониторов не активна";
        return false;
    }

    // 2. D3D device on the GPU that renders the virtual monitors; it must also drive the panel.
    auto adapter = FindAdapterForOutput(names[0]);
    if (!adapter) { error = "Не найден GPU виртуальных мониторов"; return false; }
    DXGI_ADAPTER_DESC1 ad{};
    adapter->GetDesc1(&ad);
    if (ad.AdapterLuid.LowPart != panel_.key.adapterId.LowPart || ad.AdapterLuid.HighPart != panel_.key.adapterId.HighPart)
        LogWarn("Virtual monitors render on '{}', the panel is on another adapter", Narrow(ad.Description));
    com_ptr<ID3D11Device> device;
    D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 2,
                                   D3D11_SDK_VERSION, device.put(), nullptr, nullptr);
    if (FAILED(hr)) { error = std::format("D3D11CreateDevice: 0x{:08X}", (unsigned)hr); return false; }
    if (auto dxgiDev = device.try_as<IDXGIDevice>()) dxgiDev->SetGPUThreadPriority(7); // never miss a vblank

    // 3. Presenter.
    std::unique_ptr<IPresenter> presenter;
    HWND coverWindow = nullptr;
    RECT panelRect{};
    if (presentation_ == Presentation::Exclusive) {
        if (!SetPanelRemovedFromDesktop(panel_, true)) { error = "Не удалось убрать монитор с рабочего стола"; return false; }
        bool ready = false;
        for (int i = 0; i < 40 && !ready; ++i) {
            PumpFor(250);
            ready = DisplayCorePresenter::IsTargetAvailable(panel_.devicePath);
        }
        if (!ready) { error = "Монитор не стал доступен для монопольного вывода"; return false; }
        presenter = std::make_unique<DisplayCorePresenter>(panel_.devicePath);
    } else if (presentation_ == Presentation::Window) {
        livePanel = FindPhysicalPanel(all);
        if (!livePanel || !livePanel->active) { error = "Монитор пропал с рабочего стола"; return false; }
        panelRect = {livePanel->x, livePanel->y, livePanel->x + livePanel->width, livePanel->y + livePanel->height};
        auto wp = std::make_unique<WindowPresenter>(W, H, panel_.refreshHz, panelRect);
        presenter = std::move(wp);
    } else {
        presenter = std::make_unique<WindowPresenter>(W, H, panel_.refreshHz, std::nullopt);
    }
    auto* windowPresenter = dynamic_cast<WindowPresenter*>(presenter.get());

    if (!compositor_.Start(device, std::move(presenter), names, zones_, layout.showDividers)) {
        error = "Не удалось запустить компоновщик";
        return false;
    }
    if (presentation_ == Presentation::Window) {
        coverWindow = windowPresenter ? windowPresenter->Window() : nullptr;
        const auto& last = zones_.back();
        RECT row{0, 0, last.x + last.width, last.height};
        RECT home{zones_[0].x, 0, zones_[0].x + zones_[0].width, zones_[0].height};
        guard_.Start(panelRect, row, home, coverWindow);
        guard_.RelocateStrayWindows();
    }
    return true;
}

bool Engine::Apply(const Layout& layout, std::string& error) {
    config_.layout = layout;
    SaveConfig(config_);
    if (layout.mode == Mode::Merged) {
        RestoreMerged();
        return true;
    }
    // Changing the layout: start from a clean merged state.
    if (mode_ == Mode::Split) RestoreMerged();
    if (!EnterSplit(layout, error)) {
        LogError("Split failed: {}", error);
        lastError_ = error;
        RestoreMerged();
        return false;
    }
    mode_ = Mode::Split;
    config_.layout.mode = Mode::Split;
    lastError_.clear();
    LogInfo("Split into {} zones", zones_.size());
    return true;
}

bool Engine::ApplyPreset(const std::string& id, std::string& error) {
    for (auto& p : DefaultPresets()) {
        if (p.id != id) continue;
        Layout l = config_.layout;
        l.ratios = p.ratios;
        l.presetId = p.id;
        l.mode = Mode::Split;
        return Apply(l, error);
    }
    error = "Неизвестный пресет: " + id;
    return false;
}

bool Engine::ToggleMode(std::string& error) {
    Layout l = config_.layout;
    l.mode = mode_ == Mode::Split ? Mode::Merged : Mode::Split;
    return Apply(l, error);
}

void Engine::RestoreMerged() {
    guard_.Stop();
    compositor_.Stop(); // closes the cover window / releases the panel back to Windows
    auto all = EnumerateDisplays();
    const DisplayInfo* panel = FindPhysicalPanel(all);
    if (!panel && havePanel_) panel = &panel_;
    if (panel) {
        SetPanelRemovedFromDesktop(*panel, false);
        // Wait for the panel to be on the desktop before dropping the virtual monitors,
        // otherwise Windows could briefly have no display at all.
        for (int i = 0; i < 40; ++i) {
            all = EnumerateDisplays();
            panel = FindPhysicalPanel(all);
            if (panel && panel->active) break;
            if (panel && i == 8) AttachTargets({panel->key});
            PumpFor(250);
        }
    }
    if (panel && panel->active) {
        // Panel primary first so Windows moves every window onto it, then drop the virtual monitors.
        MakePanelPrimary(*panel);
        PumpFor(300);
        DetachVirtualDisplays();
        PumpFor(300);
        all = EnumerateDisplays();
        if (auto p = FindPhysicalPanel(all)) {
            if (p->x != 0 || p->y != 0) MakePanelPrimary(*p);
            panel_ = *p;
            havePanel_ = true;
        }
    } else {
        LogError("RestoreMerged: panel did not come back; leaving virtual monitors attached");
    }
    mode_ = Mode::Merged;
    config_.layout.mode = Mode::Merged;
    zones_.clear();
    LogInfo("Merged mode");
}

void Engine::Tick() {
    if (mode_ != Mode::Split) return;
    if (compositor_.Failed() || !compositor_.Running()) {
        LogError("Compositor stopped unexpectedly; restoring the panel");
        RestoreMerged();
        lastError_ = "Компоновщик остановился, экран возвращён в обычный режим";
        return;
    }
    if (presentation_ == Presentation::Window) guard_.RelocateStrayWindows();
}

Json Engine::Status() {
    Json s(Json::Object{});
    s.set("mode", ModeName(mode_));
    s.set("presentation", PresentationName(mode_ == Mode::Split ? presentation_ : ChoosePresentation()));
    s.set("exclusiveLicensed", licensed_);
    s.set("vddInstalled", vdd_.IsInstalled());
    s.set("watchdog", watchdog_);
    s.set("error", lastError_);
    if (havePanel_) {
        Json p(Json::Object{});
        p.set("name", Narrow(panel_.friendlyName));
        p.set("width", panel_.nativeWidth);
        p.set("height", panel_.nativeHeight);
        p.set("refreshHz", panel_.refreshHz);
        s.set("panel", p);
    }
    Json::Array z;
    for (auto& r : zones_) {
        Json zj(Json::Object{});
        zj.set("x", r.x);
        zj.set("width", r.width);
        zj.set("height", r.height);
        z.push_back(zj);
    }
    s.set("zones", Json(std::move(z)));
    s.set("fps", compositor_.Stats().fps.load());
    s.set("frameMs", compositor_.Stats().frameMs.load());
    s.set("layout", LayoutToJson(config_.layout));
    Json::Array presets;
    for (auto& p : DefaultPresets()) {
        Json pj(Json::Object{});
        pj.set("id", p.id);
        pj.set("name", p.name);
        Json::Array r;
        for (double v : p.ratios) r.push_back(Json(v));
        pj.set("ratios", Json(std::move(r)));
        presets.push_back(pj);
    }
    s.set("presets", Json(std::move(presets)));
    return s;
}

} // namespace gs
