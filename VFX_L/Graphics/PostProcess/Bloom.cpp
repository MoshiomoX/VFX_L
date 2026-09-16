// ============================================================
// Bloom.cpp
// ============================================================
#include "Graphics/PostProcess/Bloom.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Graphics/Renderer/RenderStates.h"
#include <iostream>

namespace
{
    constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
}

bool Bloom::Initialize(ID3D11Device* device, int width, int height)
{
    m_CS = std::make_shared<ComputeShader>();
    HRESULT hr = ShaderPath::Load(m_CS.get(), device, L"Shader/PostProcess/BloomCS.hlsl");
    std::cout << "[Bloom] BloomCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) { m_CS.reset(); return false; }

    return CreateMips(device, width, height);
}

bool Bloom::Resize(ID3D11Device* device, int width, int height)
{
    m_Down.clear();
    m_Up.clear();
    return CreateMips(device, width, height);
}

void Bloom::Shutdown()
{
    m_Down.clear();
    m_Up.clear();
    m_CS.reset();
}

// ============================================================
// mip 鎖を作る。[0] が半解像度、以降半分ずつ
// ============================================================
bool Bloom::CreateMips(ID3D11Device* device, int width, int height)
{
    auto make = [&](Mip& m, UINT w, UINT h) -> bool
        {
            m.w = (w < 1) ? 1 : w;
            m.h = (h < 1) ? 1 : h;

            D3D11_TEXTURE2D_DESC td = {};
            td.Width = m.w;
            td.Height = m.h;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = kFormat;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device->CreateTexture2D(&td, nullptr, &m.tex))) return false;
            if (FAILED(device->CreateShaderResourceView(m.tex.Get(), nullptr, &m.srv))) return false;
            if (FAILED(device->CreateUnorderedAccessView(m.tex.Get(), nullptr, &m.uav))) return false;
            return true;
        };

    m_Down.resize(kMipCount);
    m_Up.resize(kMipCount - 1);

    UINT w = (UINT)width / 2, h = (UINT)height / 2;
    for (int i = 0; i < kMipCount; ++i)
    {
        if (!make(m_Down[i], w, h))
        {
            std::cout << "[Error] Bloom: mip " << i << " failed" << std::endl;
            return false;
        }
        if (i < kMipCount - 1 && !make(m_Up[i], w, h)) return false;
        w /= 2; h /= 2;
    }
    return true;
}

// ============================================================
// 1 dispatch 分
// src    : t0（mode 0 は場面の SRV、それ以外は srcMip の SRV）
// srcMip : texel サイズを取るため。mode 0 なら nullptr（場面は dst の 2 倍として扱う）
// baseMip: mode 2 のみ。dst と同サイズの降採様結果
// ============================================================
void Bloom::Run(ID3D11DeviceContext* context, Mode mode,
    ID3D11ShaderResourceView* src, const Mip* srcMip,
    const Mip* baseMip, const Mip& dst)
{
    CB cb = {};
    cb.dstTexelX = 1.0f / dst.w;
    cb.dstTexelY = 1.0f / dst.h;
    cb.srcTexelX = srcMip ? 1.0f / srcMip->w : 0.5f / dst.w;
    cb.srcTexelY = srcMip ? 1.0f / srcMip->h : 0.5f / dst.h;
    cb.threshold = m_Params.threshold;
    cb.knee = m_Params.knee;
    cb.mode = mode;

    m_CS->WriteBuffer(context, 0, &cb);
    m_CS->Bind(context);
    m_CS->SetSRV(context, "src", src);
    if (baseMip)
        m_CS->SetSRV(context, "srcBase", baseMip->srv.Get());
    m_CS->SetUAV(context, "dst", dst.uav.Get());
    m_CS->BindUAVs(context);

    context->Dispatch((dst.w + 7) / 8, (dst.h + 7) / 8, 1);

    m_CS->UnbindSRVs(context);
    m_CS->UnbindUAVs(context);
}

// ============================================================
// 実行: 9 dispatch
// ============================================================
void Bloom::Execute(ID3D11DeviceContext* context, ID3D11ShaderResourceView* sceneSRV)
{
    if (!m_CS || m_Down.empty()) return;

    ID3D11SamplerState* samp = RenderStates::Get().LinearClamp();
    context->CSSetSamplers(0, 1, &samp);

    const int N = kMipCount;

    // 1) prefilter: 場面 → Down[0]
    Run(context, Prefilter, sceneSRV, nullptr, nullptr, m_Down[0]);

    // 2) 降採様: Down[i] → Down[i+1]
    for (int i = 0; i < N - 1; ++i)
        Run(context, Downsample, m_Down[i].srv.Get(), &m_Down[i], nullptr, m_Down[i + 1]);

    // 3) 昇採様: Up[i] = Down[i] + blur(小さい方)。一番下は Down[N-1] を小さい方にする
    for (int i = N - 2; i >= 0; --i)
    {
        const Mip& lower = (i == N - 2) ? m_Down[N - 1] : m_Up[i + 1];
        Run(context, Upsample, lower.srv.Get(), &lower, &m_Down[i], m_Up[i]);
    }
}

ID3D11ShaderResourceView* Bloom::GetResultSRV() const
{
    return m_Up.empty() ? nullptr : m_Up[0].srv.Get();
}