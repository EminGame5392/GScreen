#include "Capture.h"
#include "Log.h"

namespace gs {

using winrt::com_ptr;

static com_ptr<IDXGIOutput> FindOutput(IDXGIAdapter1* adapter, const std::wstring& gdiName) {
    com_ptr<IDXGIOutput> out;
    for (UINT i = 0; adapter->EnumOutputs(i, out.put()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_OUTPUT_DESC d{};
        out->GetDesc(&d);
        if (_wcsicmp(d.DeviceName, gdiName.c_str()) == 0) return out;
        out = nullptr;
    }
    return nullptr;
}

com_ptr<IDXGIAdapter1> FindAdapterForOutput(const std::wstring& gdiName) {
    com_ptr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())))) return nullptr;
    com_ptr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++i) {
        if (FindOutput(adapter.get(), gdiName)) return adapter;
        adapter = nullptr;
    }
    return nullptr;
}

bool OutputCapture::Init(ID3D11Device* device, const std::wstring& gdiName) {
    dupl_ = nullptr;
    frame_ = nullptr;
    gdiName_ = gdiName;
    device_.copy_from(device);
    device->GetImmediateContext(context_.put());

    com_ptr<IDXGIDevice> dxgiDevice = device_.as<IDXGIDevice>();
    com_ptr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(adapter.put());
    auto output = FindOutput(adapter.as<IDXGIAdapter1>().get(), gdiName);
    if (!output) {
        LogError("Output {} is not on the compositor GPU", Narrow(gdiName));
        return false;
    }
    auto output5 = output.try_as<IDXGIOutput5>();
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
    HRESULT hr = output5 ? output5->DuplicateOutput1(device, 0, 1, formats, dupl_.put())
                         : output.as<IDXGIOutput1>()->DuplicateOutput(device, dupl_.put());
    if (FAILED(hr)) {
        LogError("DuplicateOutput({}) failed: 0x{:08X}", Narrow(gdiName), (unsigned)hr);
        return false;
    }
    DXGI_OUTDUPL_DESC dd{};
    dupl_->GetDesc(&dd);
    width_ = (int)dd.ModeDesc.Width;
    height_ = (int)dd.ModeDesc.Height;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, frame_.put()))) return false;
    LogInfo("Capturing {} at {}x{}", Narrow(gdiName), width_, height_);
    return true;
}

bool OutputCapture::Update() {
    if (!dupl_) return false;
    DXGI_OUTDUPL_FRAME_INFO info{};
    com_ptr<IDXGIResource> res;
    HRESULT hr = dupl_->AcquireNextFrame(0, &info, res.put());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return true; // nothing new
    if (FAILED(hr)) {
        LogWarn("AcquireNextFrame({}) = 0x{:08X}, reinitialising", Narrow(gdiName_), (unsigned)hr);
        dupl_ = nullptr;
        return false;
    }
    if (info.LastPresentTime.QuadPart != 0) {
        auto tex = res.as<ID3D11Texture2D>();
        context_->CopyResource(frame_.get(), tex.get());
    }
    if (info.LastMouseUpdateTime.QuadPart != 0) {
        pointer_.visible = info.PointerPosition.Visible != 0;
        pointer_.position = info.PointerPosition.Position;
        pointer_.lastUpdate = info.LastMouseUpdateTime;
        if (info.PointerShapeBufferSize) UpdatePointerShape(info);
    }
    dupl_->ReleaseFrame();
    return true;
}

void OutputCapture::UpdatePointerShape(const DXGI_OUTDUPL_FRAME_INFO& info) {
    shapeBuffer_.resize(info.PointerShapeBufferSize);
    UINT needed = 0;
    DXGI_OUTDUPL_POINTER_SHAPE_INFO si{};
    if (FAILED(dupl_->GetFramePointerShape((UINT)shapeBuffer_.size(), shapeBuffer_.data(), &needed, &si))) return;

    int w = (int)si.Width;
    int h = si.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME ? (int)si.Height / 2 : (int)si.Height;
    if (w <= 0 || h <= 0) return;
    std::vector<UINT32> px((size_t)w * h, 0);

    if (si.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                UINT32 c = *(UINT32*)&shapeBuffer_[y * si.Pitch + x * 4];
                // Premultiply so the blend is ONE / INV_SRC_ALPHA.
                UINT32 a = c >> 24;
                UINT32 r = ((c >> 16) & 0xFF) * a / 255, g = ((c >> 8) & 0xFF) * a / 255, b = (c & 0xFF) * a / 255;
                px[y * w + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
    } else if (si.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
        // Mask bit set = XOR with screen. We cannot read the screen here, so XOR pixels are drawn opaque.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                UINT32 c = *(UINT32*)&shapeBuffer_[y * si.Pitch + x * 4];
                bool xorPx = (c >> 24) != 0;
                UINT32 rgb = c & 0x00FFFFFF;
                px[y * w + x] = xorPx ? (rgb ? (0xFF000000 | rgb) : 0) : (0xFF000000 | rgb);
            }
    } else { // monochrome: AND mask then XOR mask, 1 bpp
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                BYTE bit = (BYTE)(0x80 >> (x % 8));
                bool andM = shapeBuffer_[y * si.Pitch + x / 8] & bit;
                bool xorM = shapeBuffer_[(y + h) * si.Pitch + x / 8] & bit;
                UINT32 v;
                if (!andM && !xorM) v = 0xFF000000;            // black
                else if (!andM && xorM) v = 0xFFFFFFFF;        // white
                else if (andM && !xorM) v = 0;                 // transparent
                else v = 0xFFFFFFFF;                           // inverted → white
                px[y * w + x] = v;
            }
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{px.data(), (UINT)w * 4, 0};
    com_ptr<ID3D11Texture2D> tex;
    if (FAILED(device_->CreateTexture2D(&td, &sd, tex.put()))) return;
    pointer_.shape = nullptr;
    device_->CreateShaderResourceView(tex.get(), nullptr, pointer_.shape.put());
    pointer_.shapeW = w;
    pointer_.shapeH = h;
    pointer_.hotspot = si.HotSpot;
}

} // namespace gs
