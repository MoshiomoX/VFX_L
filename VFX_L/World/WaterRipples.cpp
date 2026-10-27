// ============================================================
// WaterRipples.cpp
// WaterSurface の波紋：フィールド全体の波動方程式（Shader/Water/WaterRippleCS.hlsl）を固定の 1/60 秒で進める。
//   テクスチャ = R16G16F（x = 今の高さ m、y = 1 歩前の高さ）を 2 枚交互。水たまりの外（マスクが 0）は常に 0。
//   波紋を起こす物は 1 フレームの最初の 1 歩だけで足す：CPU の押し（足・着地。AddRipple）、
//   GPU の範囲が出た瞬間（tickNow の「最初の tick」。kAreaShake = 爆発は強く、他は弱く）、雨粒（CS の乱数）
// ============================================================
#include "World/WaterSurface.h"
#include "World/TerrainBuild.h"
#include "Graphics/Shader/ComputeShader.h"

#include <DirectXPackedVector.h>

using Vector4 = WaterSurface::Vector4;

namespace
{
    constexpr float kSimDt = 1.0f / 60.0f;   // 波の 1 歩（固定）
    constexpr int kMaxStepsPerFrame = 3;     // これより遅れたら捨てる（重いフレームで波が暴れない）
    constexpr float kRainCell = 0.8f;        // 雨粒の格子 m（1 マスに 1 歩で多くて 1 粒）
    constexpr float kRainRadius = 0.22f;     // 雨粒の輪の始まりの半径 m
    constexpr float kRestore = 0.004f;       // 1 歩ごとに高さを 0 へ戻す割合（全体が沈んだままにならない）
}

bool WaterSurface::CreateRippleTargets()
{
    for (int i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = kRippleSize;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_RippleTex[i]))) return false;
        if (FAILED(m_Device->CreateShaderResourceView(m_RippleTex[i].Get(), nullptr, &m_RippleSRV[i]))) return false;
        if (FAILED(m_Device->CreateUnorderedAccessView(m_RippleTex[i].Get(), nullptr, &m_RippleUAV[i]))) return false;
    }
    ClearRipples();
    return true;
}

void WaterSurface::ClearRipples()
{
    if (!m_Context) return;
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 2; ++i)
        if (m_RippleUAV[i]) m_Context->ClearUnorderedAccessViewFloat(m_RippleUAV[i].Get(), zero);
    m_SimAccum = 0.0f;
}

// 水たまりのマス = 1（texel の中心がどのマスに入るか）。フィールドは正方形のつもり（texel は x / z 同じ大きさ）
void WaterSurface::BuildMask(const TerrainBuild::ReliefField& rf)
{
    m_MaskTex.Reset();
    m_MaskSRV.Reset();
    if (!m_Device) return;

    std::vector<uint8_t> mask((size_t)kRippleSize * kRippleSize, 0);
    if (rf.gw > 0 && rf.pond.size() == (size_t)rf.gw * rf.gd)
    {
        const float tx = m_MapSize.x / kRippleSize, tz = m_MapSize.y / kRippleSize;
        for (int v = 0; v < kRippleSize; ++v)
        {
            const float z = m_MapOrigin.y + (v + 0.5f) * tz;
            const int gz = (int)std::floor((z - rf.oz) / TerrainBuild::kCs);
            if (gz < 0 || gz >= rf.gd) continue;
            for (int u = 0; u < kRippleSize; ++u)
            {
                const float x = m_MapOrigin.x + (u + 0.5f) * tx;
                const int gx = (int)std::floor((x - rf.ox) / TerrainBuild::kCs);
                if (gx < 0 || gx >= rf.gw) continue;
                if (rf.pond[(size_t)gz * rf.gw + gx]) mask[(size_t)v * kRippleSize + u] = 255;
            }
        }
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = kRippleSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = mask.data();
    sd.SysMemPitch = kRippleSize;
    if (SUCCEEDED(m_Device->CreateTexture2D(&td, &sd, &m_MaskTex)))
        m_Device->CreateShaderResourceView(m_MaskTex.Get(), nullptr, &m_MaskSRV);
}

void WaterSurface::StepRipples(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates)
{
    if (!m_RippleCS || !m_MaskSRV || !m_Context) return;
    int steps = (int)(m_SimAccum / kSimDt);
    if (steps <= 0) return;   // 押しは次のフレームまで持ち越す
    if (steps > kMaxStepsPerFrame) { steps = kMaxStepsPerFrame; m_SimAccum = 0.0f; }
    else m_SimAccum -= steps * kSimDt;

    const auto& s = m_Settings;
    RippleCB cb = {};
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.texel = m_MapSize.x / (float)kRippleSize;
    const float c = (std::max)(s.rippleSpeed, 0.0f) * kSimDt / cb.texel;
    cb.courant2 = (std::min)(c * c, 0.45f);   // 陽解法の安定の限界（0.5）より手前
    cb.keep = std::exp(-(std::max)(s.rippleDamping, 0.0f) * kSimDt);
    cb.restore = kRestore;
    cb.rainCell = kRainCell;
    cb.rainProb = std::clamp(s.rainRate * std::clamp(m_Rain, 0.0f, 1.0f) * kRainCell * kRainCell * kSimDt, 0.0f, 1.0f);
    cb.rainStrength = s.rainStrength;
    cb.rainRadius = kRainRadius;
    cb.blastStrength = s.blastStrength;
    cb.areaStrength = s.areaStrength;
    const uint32_t n = (uint32_t)(std::min)((int)m_Impulses.size(), kMaxImpulses);
    for (uint32_t i = 0; i < n; ++i) cb.impulse[i] = m_Impulses[i];

    ID3D11DeviceContext* ctx = m_Context;
    for (int k = 0; k < steps; ++k)
    {
        cb.step = m_Step++;
        cb.sources = (k == 0) ? 1u : 0u;   // 押し・範囲・雨粒は最初の 1 歩だけ
        cb.impulseCount = (k == 0) ? n : 0u;
        m_RippleCS->WriteBuffer(ctx, 0, &cb);
        m_RippleCS->Bind(ctx);
        m_RippleCS->SetSRV(ctx, "g_In", m_RippleSRV[m_Cur].Get());
        m_RippleCS->SetSRV(ctx, "g_Mask", m_MaskSRV.Get());
        m_RippleCS->SetSRV(ctx, "g_Areas", areas);
        m_RippleCS->SetSRV(ctx, "g_AreaStates", areaStates);
        m_RippleCS->SetUAV(ctx, "g_Out", m_RippleUAV[1 - m_Cur].Get());
        m_RippleCS->BindUAVs(ctx);
        ctx->Dispatch((kRippleSize + 7) / 8, (kRippleSize + 7) / 8, 1);
        m_RippleCS->UnbindSRVs(ctx);
        m_RippleCS->UnbindUAVs(ctx);
        m_Cur = 1 - m_Cur;
    }
    m_Impulses.clear();
}

// TEMP-TEST: 自動テストの検証用。Map で GPU を待つので毎フレーム呼ばない
bool WaterSurface::DebugReadRipples(float& maxAbs, uint32_t& moving) const
{
    maxAbs = 0.0f;
    moving = 0;
    if (!m_Device || !m_Context || !m_RippleTex[m_Cur]) return false;
    if (!m_Staging)
    {
        D3D11_TEXTURE2D_DESC td = {};
        m_RippleTex[m_Cur]->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_Staging))) return false;
    }
    m_Context->CopyResource(m_Staging.Get(), m_RippleTex[m_Cur].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const float threshold = m_Settings.rippleCrest * 0.25f;
    for (int v = 0; v < kRippleSize; ++v)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + (size_t)v * mapped.RowPitch);
        for (int u = 0; u < kRippleSize; ++u)
        {
            const float h = std::abs(DirectX::PackedVector::XMConvertHalfToFloat(row[u * 2]));
            maxAbs = (std::max)(maxAbs, h);
            if (h > threshold) ++moving;
        }
    }
    m_Context->Unmap(m_Staging.Get(), 0);
    return true;
}
