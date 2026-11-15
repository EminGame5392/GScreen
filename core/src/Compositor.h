#pragma once
#include "Capture.h"
#include "Layout.h"
#include "Presenter.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace gs {

struct CompositorStats {
    std::atomic<double> fps{0};
    std::atomic<double> frameMs{0};  // CPU time spent composing one frame
    std::atomic<int> zones{0};
};

// Runs on its own thread: capture every virtual monitor → copy into its zone → draw cursor → present at panel vblank.
class Compositor {
public:
    ~Compositor() { Stop(); }

    // gdiNames[i] is shown in zones[i]. The presenter decides where the panel is.
    bool Start(winrt::com_ptr<ID3D11Device> device, std::unique_ptr<IPresenter> presenter,
               std::vector<std::wstring> gdiNames, std::vector<ZoneRect> zones, bool showDividers);
    void Stop();
    bool Running() const { return running_; }
    // True if the thread stopped because of an unrecoverable error.
    bool Failed() const { return failed_; }
    const CompositorStats& Stats() const { return stats_; }

private:
    void Loop();
    bool CreateCursorPipeline();
    void DrawCursor(ID3D11RenderTargetView* rtv, const PointerState& p, const ZoneRect& zone);

    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext1> ctx_;
    std::unique_ptr<IPresenter> presenter_;
    std::vector<std::wstring> names_;
    std::vector<ZoneRect> zones_;
    std::vector<OutputCapture> captures_;
    bool showDividers_ = false;

    winrt::com_ptr<ID3D11VertexShader> vs_;
    winrt::com_ptr<ID3D11PixelShader> ps_;
    winrt::com_ptr<ID3D11BlendState> blend_;
    winrt::com_ptr<ID3D11RasterizerState> raster_;
    winrt::com_ptr<ID3D11SamplerState> sampler_;
    winrt::com_ptr<ID3D11Buffer> cb_;

    std::thread thread_;
    std::atomic<bool> running_{false}, stop_{false}, failed_{false};
    CompositorStats stats_;
};

} // namespace gs
