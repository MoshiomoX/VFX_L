// ============================================================
// SceneSnapshot.cpp
// ============================================================
#include "Graphics/PostProcess/SceneSnapshot.h"
#include "Graphics/Graphics.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"

using namespace DirectX::SimpleMath;

namespace
{
    // HLSL の BackdropCB と同じ並び（48 bytes）
    struct BackdropCB
    {
        Vector2 photoCenter;
        float zoom;
        float brightness;
        float shadeStrength;
        float shadeFrom;
        float shadeTo;
        float vignette;
        float desaturate;
        float bottomShade;
        float mode;
        float hasPhoto;
    };
    static_assert(sizeof(BackdropCB) == 48, "BackdropCB layout mismatch");
}

SceneSnapshot& SceneSnapshot::Get()
{
    static SceneSnapshot s;
    return s;
}

// ============================================================
// シーンの HDR RT（MSAA）→ 自分のテクスチャへ resolve。大きさが変わったら作り直す
// ============================================================
bool SceneSnapshot::Capture(Graphics& gfx)
{
    ID3D11RenderTargetView* rtv = gfx.GetSceneRTV();
    ID3D11Device* device = gfx.GetDevice();
    ID3D11DeviceContext* ctx = gfx.GetContext();
    if (!rtv || !device || !ctx) return false;

    Microsoft::WRL::ComPtr<ID3D11Resource> res;
    rtv->GetResource(&res);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> src;
    if (!res || FAILED(res.As(&src))) return false;

    D3D11_TEXTURE2D_DESC sd = {};
    src->GetDesc(&sd);

    if (!m_Tex || m_Width != sd.Width || m_Height != sd.Height)
    {
        m_Tex.Reset();
        m_SRV.Reset();
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = sd.Width;
        td.Height = sd.Height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = sd.Format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &m_Tex))) { m_Tex.Reset(); return false; }
        if (FAILED(device->CreateShaderResourceView(m_Tex.Get(), nullptr, &m_SRV))) { m_Tex.Reset(); m_SRV.Reset(); return false; }
        m_Width = sd.Width;
        m_Height = sd.Height;
    }

    if (sd.SampleDesc.Count > 1)
        ctx->ResolveSubresource(m_Tex.Get(), 0, src.Get(), 0, sd.Format);
    else
        ctx->CopyResource(m_Tex.Get(), src.Get());
    return true;
}

void SceneSnapshot::Clear()
{
    m_Tex.Reset();
    m_SRV.Reset();
    m_Width = m_Height = 0;
    shotTime = 0.0f;
    highlight = false;
}

void SceneSnapshot::Shutdown()
{
    Clear();
    m_VS.reset();
    m_PS.reset();
    m_ScrimBlend.Reset();
    m_PassTried = false;
}

bool SceneSnapshot::EnsurePass(ID3D11Device* device)
{
    if (m_PassTried) return m_VS && m_PS && m_ScrimBlend;
    m_PassTried = true;

    m_VS = std::make_shared<VertexShader>();
    m_PS = std::make_shared<PixelShader>();
    // 全画面の三角形は空と同じ物
    bool ok = SUCCEEDED(ShaderPath::Load(m_VS.get(), device, L"Shader/Sky/SkyVS.hlsl"))
           && SUCCEEDED(ShaderPath::Load(m_PS.get(), device, L"Shader/PostProcess/BackdropPS.hlsl"));

    // scrim：結果 = 出力（黒 × a = 0）+ 下の色 × (1 − a)
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bd, &m_ScrimBlend));

    std::cout << "[SceneSnapshot] backdrop pass " << (ok ? "OK" : "FAILED") << std::endl;
    if (!ok) { m_VS.reset(); m_PS.reset(); m_ScrimBlend.Reset(); }
    return ok;
}

// ============================================================
// 全画面の背景。シーンの HDR RT が刺さっている時（シーンの描画中）に呼ぶ
// ============================================================
void SceneSnapshot::DrawBackdrop(ID3D11DeviceContext* ctx, Graphics& gfx, const Backdrop& b)
{
    if (!ctx || !EnsurePass(gfx.GetDevice())) return;

    BackdropCB cb = {};
    cb.photoCenter = b.photoCenter;
    cb.zoom = b.zoom;
    cb.brightness = b.brightness;
    cb.shadeStrength = b.shadeStrength;
    cb.shadeFrom = b.shadeFrom;
    cb.shadeTo = b.shadeTo;
    cb.vignette = b.vignette;
    cb.desaturate = b.desaturate;
    cb.bottomShade = b.bottomShade;
    cb.mode = b.scrim ? 1.0f : 0.0f;
    cb.hasPhoto = (!b.scrim && m_SRV) ? 1.0f : 0.0f;
    m_PS->WriteBuffer(ctx, 0, &cb);

    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    ID3D11ShaderResourceView* srv = (cb.hasPhoto > 0.5f) ? m_SRV.Get() : nullptr;
    ctx->PSSetShaderResources(0, 1, &srv);
    auto& rs = RenderStates::Get();
    ID3D11SamplerState* samp = rs.LinearClamp();
    ctx->PSSetSamplers(0, 1, &samp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(b.scrim ? m_ScrimBlend.Get() : rs.Opaque(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(rs.DepthNone(), 0);
    ctx->RSSetState(rs.CullNone());
    ctx->Draw(3, 0);

    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    rs.Restore(ctx);
}
