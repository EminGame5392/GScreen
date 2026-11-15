#pragma once
#include "Presenter.h"
#include <dxgi1_6.h>
#include <optional>

namespace gs {

// Presents the composed frame through a flip-model swap chain on a window.
//  * cover mode (Windows Home/Pro): a borderless topmost window exactly over the panel, which stays a desktop
//    monitor parked in a corner. DWM can scan the swap chain out directly (independent flip).
//  * preview mode (testing): an ordinary resizable window at 1/3 scale.
class WindowPresenter : public IPresenter {
public:
    WindowPresenter(int width, int height, double refreshHz, std::optional<RECT> cover)
        : w_(width), h_(height), hz_(refreshHz), cover_(cover) {}
    ~WindowPresenter() override;
    bool Init(ID3D11Device* device) override; // must be called on a thread that pumps messages
    int Width() const override { return w_; }
    int Height() const override { return h_; }
    double RefreshHz() const override { return hz_; }
    ID3D11Texture2D* BackBuffer() override;
    bool Present() override;
    HWND Window() const { return hwnd_; }

private:
    int w_, h_;
    double hz_;
    std::optional<RECT> cover_;
    HWND hwnd_ = nullptr;
    HANDLE frameLatency_ = nullptr;
    winrt::com_ptr<IDXGISwapChain2> swap_;
    winrt::com_ptr<ID3D11Texture2D> back_;
    int frameCount_ = 0;
};

} // namespace gs
