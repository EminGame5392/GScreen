#include "WindowPresenter.h"
#include "Log.h"

namespace gs {

using winrt::com_ptr;

static LRESULT CALLBACK PresenterProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;          // the engine owns the lifetime
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;              // never steal focus from the game/app
    case WM_NCHITTEST:
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) return HTTRANSPARENT;
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

WindowPresenter::~WindowPresenter() {
    swap_ = nullptr;
    if (frameLatency_) CloseHandle(frameLatency_);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool WindowPresenter::Init(ID3D11Device* device) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = PresenterProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GScreenPresenter";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    if (cover_) {
        const RECT& r = *cover_;
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
                                wc.lpszClassName, L"GScreen", WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, wc.hInstance, nullptr);
        if (hwnd_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    } else {
        RECT r{0, 0, w_ / 3, h_ / 3};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"GScreen — предпросмотр компоновки",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                                r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    }
    if (!hwnd_) return false;

    com_ptr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())))) return false;
    com_ptr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(adapter.put());
    com_ptr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(factory.put()));

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = w_;
    sd.Height = h_;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    com_ptr<IDXGISwapChain1> sc1;
    HRESULT hr = factory->CreateSwapChainForHwnd(device, hwnd_, &sd, nullptr, nullptr, sc1.put());
    if (FAILED(hr)) {
        LogError("CreateSwapChainForHwnd failed: 0x{:08X}", (unsigned)hr);
        return false;
    }
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    swap_ = sc1.as<IDXGISwapChain2>();
    swap_->SetMaximumFrameLatency(1); // one queued frame: the lowest latency that still avoids tearing
    frameLatency_ = swap_->GetFrameLatencyWaitableObject();
    return true;
}

ID3D11Texture2D* WindowPresenter::BackBuffer() {
    // Wait until DXGI can take a frame so we compose with the freshest captured content.
    if (frameLatency_) WaitForSingleObjectEx(frameLatency_, 100, TRUE);
    back_ = nullptr;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(back_.put())))) return nullptr;
    return back_.get();
}

bool WindowPresenter::Present() {
    back_ = nullptr;
    HRESULT hr = swap_->Present(1, 0);
    // Other topmost windows (overlays, notifications) can end up above us; re-assert about once a second.
    if (cover_ && ++frameCount_ % 240 == 0)
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
}

} // namespace gs
