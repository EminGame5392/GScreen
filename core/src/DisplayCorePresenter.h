#pragma once
#include "Presenter.h"
#include <d3d11_4.h>
#include <winrt/Windows.Devices.Display.Core.h>
#include <string>

namespace gs {

// Owns the physical panel through Windows.Devices.Display.Core and scans out our own surfaces.
// The panel has to be removed from the desktop first (see Specialization.h); then Windows hands it to us exclusively.
class DisplayCorePresenter : public IPresenter {
public:
    explicit DisplayCorePresenter(std::wstring monitorDevicePath) : devicePath_(std::move(monitorDevicePath)) {}
    ~DisplayCorePresenter() override;
    bool Init(ID3D11Device* device) override;
    int Width() const override { return w_; }
    int Height() const override { return h_; }
    double RefreshHz() const override { return hz_; }
    ID3D11Texture2D* BackBuffer() override;
    bool Present() override;

    // Finds the special-purpose target for this device path. Exposed so the engine can wait for it to appear.
    static bool IsTargetAvailable(const std::wstring& monitorDevicePath);

private:
    std::wstring devicePath_;
    int w_ = 0, h_ = 0;
    double hz_ = 0;
    int cur_ = 0;
    UINT64 fenceValue_ = 0;

    winrt::Windows::Devices::Display::Core::DisplayManager manager_{nullptr};
    winrt::Windows::Devices::Display::Core::DisplayTarget target_{nullptr};
    winrt::Windows::Devices::Display::Core::DisplayState state_{nullptr};
    winrt::Windows::Devices::Display::Core::DisplayDevice device_{nullptr};
    winrt::Windows::Devices::Display::Core::DisplaySource source_{nullptr};
    winrt::Windows::Devices::Display::Core::DisplayTaskPool taskPool_{nullptr};
    winrt::Windows::Devices::Display::Core::DisplayFence displayFence_{nullptr};
    winrt::Windows::Devices::Display::Core::DisplayScanout scanouts_[2]{nullptr, nullptr};
    winrt::com_ptr<ID3D11Texture2D> textures_[2];
    winrt::com_ptr<ID3D11Fence> fence_;
    winrt::com_ptr<ID3D11DeviceContext4> ctx_;
};

} // namespace gs
