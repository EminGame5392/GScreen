#include "DisplayCorePresenter.h"
#include "Log.h"
#include <windows.devices.display.core.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Devices.Display.h>

namespace gs {

using namespace winrt::Windows::Devices::Display;
using namespace winrt::Windows::Devices::Display::Core;
using winrt::Windows::Graphics::DirectX::DirectXColorSpace;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::com_ptr;

static bool PathMatches(const DisplayTarget& t, const std::wstring& devicePath) {
    if (auto mon = t.TryGetMonitor()) {
        std::wstring id(mon.DeviceId());
        return _wcsicmp(id.c_str(), devicePath.c_str()) == 0;
    }
    return false;
}

static DisplayTarget FindSpecialTarget(const DisplayManager& mgr, const std::wstring& devicePath) {
    for (auto const& t : mgr.GetCurrentTargets()) {
        if (!t.IsConnected()) continue;
        if (t.UsageKind() != DisplayMonitorUsageKind::SpecialPurpose) continue;
        if (devicePath.empty() || PathMatches(t, devicePath)) return t;
    }
    return nullptr;
}

bool DisplayCorePresenter::IsTargetAvailable(const std::wstring& monitorDevicePath) {
    try {
        auto mgr = DisplayManager::Create(DisplayManagerOptions::None);
        bool found = FindSpecialTarget(mgr, monitorDevicePath) != nullptr;
        mgr.Close();
        return found;
    } catch (winrt::hresult_error const&) {
        return false;
    }
}

DisplayCorePresenter::~DisplayCorePresenter() {
    try {
        if (device_ && source_) device_.WaitForVBlank(source_); // let the last task finish before releasing surfaces
        scanouts_[0] = scanouts_[1] = nullptr;
        textures_[0] = textures_[1] = nullptr;
        taskPool_ = nullptr;
        source_ = nullptr;
        device_ = nullptr;
        state_ = nullptr;
        target_ = nullptr;
        if (manager_) manager_.Close(); // releases ownership; Windows takes the panel back
    } catch (...) {
    }
}

bool DisplayCorePresenter::Init(ID3D11Device* d3d) {
    try {
        manager_ = DisplayManager::Create(DisplayManagerOptions::None);
        target_ = FindSpecialTarget(manager_, devicePath_);
        if (!target_) {
            LogError("Panel is not available as a special-purpose display");
            return false;
        }

        auto acquired = manager_.TryAcquireTargetsAndCreateEmptyState(winrt::single_threaded_vector<DisplayTarget>({target_}));
        if (acquired.ErrorCode() != DisplayManagerResult::Success) {
            LogError("TryAcquireTargets failed: {}", (int)acquired.ErrorCode());
            return false;
        }
        state_ = acquired.State();

        DisplayPath path = state_.ConnectTarget(target_);
        path.IsInterlaced(false);
        path.Scaling(DisplayPathScaling::Identity);
        path.SourcePixelFormat(DirectXPixelFormat::B8G8R8A8UIntNormalized);

        // Native resolution, highest refresh rate.
        DisplayModeInfo best{nullptr};
        double bestHz = 0;
        for (auto const& m : path.FindModes(DisplayModeQueryOptions::OnlyPreferredResolution)) {
            auto rate = m.PresentationRate().VerticalSyncRate;
            double hz = rate.Denominator ? (double)rate.Numerator / rate.Denominator : 0;
            if (m.IsInterlaced() || m.SourcePixelFormat() != DirectXPixelFormat::B8G8R8A8UIntNormalized) continue;
            if (hz > bestHz) { bestHz = hz; best = m; }
        }
        if (!best) {
            LogError("No progressive BGRA mode at the preferred resolution");
            return false;
        }
        path.ApplyPropertiesFromMode(best);
        auto applied = state_.TryApply(DisplayStateApplyOptions::None);
        if (applied.Status() != DisplayStateOperationStatus::Success) {
            LogError("DisplayState.TryApply failed: {}", (int)applied.Status());
            return false;
        }
        auto res = best.SourceResolution();
        w_ = res.Width;
        h_ = res.Height;
        hz_ = bestHz;

        device_ = manager_.CreateDisplayDevice(target_.Adapter());
        source_ = device_.CreateScanoutSource(target_);
        taskPool_ = device_.CreateTaskPool();

        com_ptr<ID3D11Device5> d3d5;
        if (FAILED(d3d->QueryInterface(IID_PPV_ARGS(d3d5.put())))) {
            LogError("ID3D11Device5 not available");
            return false;
        }
        com_ptr<ID3D11DeviceContext> ctx;
        d3d->GetImmediateContext(ctx.put());
        ctx_ = ctx.as<ID3D11DeviceContext4>();

        auto interop = device_.as<IDisplayDeviceInterop>();
        DisplayPrimaryDescription desc(w_, h_, DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                       DirectXColorSpace::RgbFullG22NoneP709, false, {1, 0});
        for (int i = 0; i < 2; ++i) {
            DisplaySurface surface = device_.CreatePrimary(target_, desc);
            scanouts_[i] = device_.CreateSimpleScanout(source_, surface, 0, 1);
            HANDLE h = nullptr;
            winrt::check_hresult(interop->CreateSharedHandle(static_cast<::IInspectable*>(winrt::get_abi(surface)), nullptr, GENERIC_ALL, nullptr, &h));
            HRESULT hr = d3d5->OpenSharedResource1(h, IID_PPV_ARGS(textures_[i].put()));
            CloseHandle(h);
            winrt::check_hresult(hr);
        }

        // GPU → display synchronisation: the scanout waits for our render fence.
        winrt::check_hresult(d3d5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(fence_.put())));
        HANDLE fh = nullptr;
        winrt::check_hresult(fence_->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fh));
        winrt::com_ptr<::IInspectable> fenceObj;
        HRESULT hr = interop->OpenSharedHandle(fh, winrt::guid_of<DisplayFence>(), fenceObj.put_void());
        CloseHandle(fh);
        winrt::check_hresult(hr);
        displayFence_ = fenceObj.as<DisplayFence>();

        LogInfo("Panel acquired: {}x{} @ {:.2f} Hz", w_, h_, hz_);
        return true;
    } catch (winrt::hresult_error const& e) {
        LogError("DisplayCorePresenter::Init: 0x{:08X} {}", (unsigned)e.code(), Narrow(std::wstring(e.message())));
        return false;
    }
}

ID3D11Texture2D* DisplayCorePresenter::BackBuffer() { return textures_[cur_].get(); }

bool DisplayCorePresenter::Present() {
    try {
        ctx_->Signal(fence_.get(), ++fenceValue_);
        ctx_->Flush();
        DisplayTask task = taskPool_.CreateTask();
        task.SetScanout(scanouts_[cur_]);
        task.SetWait(displayFence_, fenceValue_);
        taskPool_.ExecuteTask(task);
        device_.WaitForVBlank(source_);
        cur_ ^= 1;
        return true;
    } catch (winrt::hresult_error const& e) {
        LogError("DisplayCorePresenter::Present: 0x{:08X}", (unsigned)e.code());
        return false;
    }
}

} // namespace gs
