#pragma once
#include <d3d11_1.h>
#include <winrt/base.h>

namespace gs {

// Shows composed frames on the physical panel.
class IPresenter {
public:
    virtual ~IPresenter() = default;
    virtual bool Init(ID3D11Device* device) = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual double RefreshHz() const = 0;
    // Render target for the next frame (B8G8R8A8, Width x Height).
    virtual ID3D11Texture2D* BackBuffer() = 0;
    // Queues the frame and blocks until the next vertical blank.
    virtual bool Present() = 0;
};

} // namespace gs
