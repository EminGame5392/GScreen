#include "Compositor.h"
#include "Log.h"
#include <d3dcompiler.h>
#include <avrt.h>
#include <chrono>

#pragma comment(lib, "avrt.lib")

namespace gs {

using winrt::com_ptr;

// Fullscreen-quad-free cursor draw: 4 vertices generated from SV_VertexID, rect passed in a constant buffer.
static const char* kCursorHlsl = R"(
cbuffer Rect : register(b0) { float4 r; }; // x0, y0, x1, y1 in NDC
Texture2D tex : register(t0);
SamplerState smp : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD; };
V vs(uint id : SV_VertexID) {
    float2 uv = float2(id & 1, id >> 1);
    V o;
    o.pos = float4(lerp(r.x, r.z, uv.x), lerp(r.y, r.w, uv.y), 0, 1);
    o.uv = uv;
    return o;
}
float4 ps(V i) : SV_Target { return tex.Sample(smp, i.uv); }
)";

bool Compositor::Start(com_ptr<ID3D11Device> device, std::unique_ptr<IPresenter> presenter,
                       std::vector<std::wstring> gdiNames, std::vector<ZoneRect> zones, bool showDividers) {
    Stop();
    device_ = std::move(device);
    presenter_ = std::move(presenter);
    names_ = std::move(gdiNames);
    zones_ = std::move(zones);
    showDividers_ = showDividers;
    failed_ = false;
    com_ptr<ID3D11DeviceContext> ctx;
    device_->GetImmediateContext(ctx.put());
    ctx_ = ctx.as<ID3D11DeviceContext1>();

    if (!presenter_->Init(device_.get())) {
        LogError("Presenter init failed");
        return false;
    }
    if (!CreateCursorPipeline()) LogWarn("Cursor pipeline unavailable; the pointer will not be drawn");

    captures_.clear();
    captures_.resize(names_.size());
    for (size_t i = 0; i < names_.size(); ++i)
        if (!captures_[i].Init(device_.get(), names_[i])) return false;

    stop_ = false;
    running_ = true;
    stats_.zones = (int)zones_.size();
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void Compositor::Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
    captures_.clear();
    presenter_.reset();
}

bool Compositor::CreateCursorPipeline() {
    com_ptr<ID3DBlob> vsb, psb, err;
    if (FAILED(D3DCompile(kCursorHlsl, strlen(kCursorHlsl), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, vsb.put(), err.put())))
        return false;
    if (FAILED(D3DCompile(kCursorHlsl, strlen(kCursorHlsl), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, psb.put(), err.put())))
        return false;
    device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vs_.put());
    device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, ps_.put());

    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE; // premultiplied
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&bd, blend_.put());

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    rd.DepthClipEnable = TRUE;
    device_->CreateRasterizerState(&rd, raster_.put());

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sd, sampler_.put());

    D3D11_BUFFER_DESC cbd{16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    device_->CreateBuffer(&cbd, nullptr, cb_.put());
    return vs_ && ps_ && blend_ && raster_ && sampler_ && cb_;
}

void Compositor::DrawCursor(ID3D11RenderTargetView* rtv, const PointerState& p, const ZoneRect& zone) {
    if (!vs_ || !p.visible || !p.shape) return;
    const float W = (float)presenter_->Width(), H = (float)presenter_->Height();
    float x0 = (float)(zone.x + p.position.x), y0 = (float)(zone.y + p.position.y);
    float x1 = x0 + p.shapeW, y1 = y0 + p.shapeH;
    float rect[4] = {x0 / W * 2 - 1, 1 - y0 / H * 2, x1 / W * 2 - 1, 1 - y1 / H * 2};
    ctx_->UpdateSubresource(cb_.get(), 0, nullptr, rect, 0, 0);

    // Clip to the zone so a cursor at the right edge of A does not bleed into B.
    D3D11_VIEWPORT vp{0, 0, W, H, 0, 1};
    D3D11_RECT sc{zone.x, zone.y, zone.x + zone.width, zone.y + zone.height};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(raster_.get());
    ctx_->RSSetScissorRects(1, &sc);
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    ctx_->OMSetBlendState(blend_.get(), nullptr, 0xFFFFFFFF);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx_->IASetInputLayout(nullptr);
    ctx_->VSSetShader(vs_.get(), nullptr, 0);
    ID3D11Buffer* cb = cb_.get();
    ctx_->VSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetShader(ps_.get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = p.shape.get();
    ctx_->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* s = sampler_.get();
    ctx_->PSSetSamplers(0, 1, &s);
    ctx_->Draw(4, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
}

void Compositor::Loop() {
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Games", &taskIndex); // low-latency scheduling class
    SetThreadDescription(GetCurrentThread(), L"GScreen compositor");

    using clock = std::chrono::steady_clock;
    auto windowStart = clock::now();
    int frames = 0;
    int reinitBackoff = 0;

    while (!stop_) {
        auto t0 = clock::now();

        // 1. Pull new frames. A lost duplication is retried every ~0.5 s (UAC prompts, mode switches).
        for (auto& c : captures_) {
            if (!c.Update() && ++reinitBackoff % 120 == 0) c.Init(device_.get(), c.Name());
        }

        // 2. Compose.
        ID3D11Texture2D* back = presenter_->BackBuffer();
        if (!back) { failed_ = true; break; }
        com_ptr<ID3D11RenderTargetView> rtv;
        device_->CreateRenderTargetView(back, nullptr, rtv.put());
        const float black[4] = {0, 0, 0, 1};
        ctx_->ClearRenderTargetView(rtv.get(), black);

        // Most recently moved pointer wins: only one zone shows the cursor.
        int pointerZone = -1;
        LONGLONG newest = -1;
        for (size_t i = 0; i < captures_.size() && i < zones_.size(); ++i) {
            auto& c = captures_[i];
            auto& z = zones_[i];
            if (c.Texture()) {
                D3D11_BOX box{0, 0, 0, (UINT)std::min(c.Width(), z.width), (UINT)std::min(c.Height(), z.height), 1};
                ctx_->CopySubresourceRegion(back, 0, z.x, z.y, 0, c.Texture(), 0, &box);
            }
            if (c.Pointer().visible && c.Pointer().lastUpdate.QuadPart > newest) {
                newest = c.Pointer().lastUpdate.QuadPart;
                pointerZone = (int)i;
            }
        }
        if (pointerZone >= 0) DrawCursor(rtv.get(), captures_[pointerZone].Pointer(), zones_[pointerZone]);

        if (showDividers_) {
            const float grey[4] = {0.35f, 0.35f, 0.38f, 1};
            for (size_t i = 1; i < zones_.size(); ++i) {
                D3D11_RECT r{zones_[i].x - 1, 0, zones_[i].x + 1, zones_[i].height};
                ctx_->ClearView(rtv.get(), grey, &r, 1);
            }
        }
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);

        auto t1 = clock::now();
        stats_.frameMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 3. Present and wait for vblank (paces the loop at the panel's refresh rate).
        if (!presenter_->Present()) {
            LogError("Present failed; stopping compositor");
            failed_ = true;
            break;
        }

        ++frames;
        auto now = clock::now();
        double secs = std::chrono::duration<double>(now - windowStart).count();
        if (secs >= 1.0) {
            stats_.fps = frames / secs;
            frames = 0;
            windowStart = now;
        }
    }
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    running_ = false;
}

} // namespace gs
