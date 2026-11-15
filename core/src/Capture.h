#pragma once
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <winrt/base.h>
#include <string>
#include <vector>

namespace gs {

struct PointerState {
    bool visible = false;
    POINT position{};          // relative to the captured output
    LARGE_INTEGER lastUpdate{};
    winrt::com_ptr<ID3D11ShaderResourceView> shape; // BGRA premultiplied, nullptr until first shape
    int shapeW = 0, shapeH = 0;
    POINT hotspot{};
};

// Desktop Duplication of one virtual monitor into a persistent texture.
class OutputCapture {
public:
    bool Init(ID3D11Device* device, const std::wstring& gdiName);
    // Pulls the newest frame if there is one (non-blocking). Returns false when duplication was lost
    // and Init has to be called again (mode change, UAC desktop, driver reload).
    bool Update();

    ID3D11Texture2D* Texture() const { return frame_.get(); }
    int Width() const { return width_; }
    int Height() const { return height_; }
    const PointerState& Pointer() const { return pointer_; }
    const std::wstring& Name() const { return gdiName_; }

private:
    void UpdatePointerShape(const DXGI_OUTDUPL_FRAME_INFO& info);

    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<IDXGIOutputDuplication> dupl_;
    winrt::com_ptr<ID3D11Texture2D> frame_;
    std::wstring gdiName_;
    int width_ = 0, height_ = 0;
    PointerState pointer_;
    std::vector<BYTE> shapeBuffer_;
};

// Finds the DXGI adapter that owns the output with this GDI name (\\.\DISPLAYn).
winrt::com_ptr<IDXGIAdapter1> FindAdapterForOutput(const std::wstring& gdiName);

} // namespace gs
