// ============================================================
// WaterIce.cpp
// WaterSurface の氷（2026-11-01、ユーザー：冰にも環境への係数を）。
//   フィールド全体 256²（0.78m / texel）の R32F 1 枚（1 = 凍ったばかり → iceLife 秒で 0）を
//   Shader/Water/WaterIceCS.hlsl が毎フレーム進める。冷たい範囲（レシピの heat < 0、凍らせる範囲）が池のマスを凍らせる。
//   氷の上は波が立たず（WaterRippleCS）、雑魚は遅くなる（WaterStirCS）。プレイヤーは遅くならない。
//   氷は水面の高さにあるが池の底はその 0.35m 下なので、そのままだと足が氷に沈む：
//   プレイヤーはシーンが氷の高さに立たせ（ApplyIceFloor）、雑魚は VS が体を持ち上げる（g_PondTop）
// ============================================================
#include "World/WaterSurface.h"
#include "World/TerrainBuild.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"

bool WaterSurface::CreateIce()
{
    m_IceCS = std::make_shared<ComputeShader>();
    if (FAILED(ShaderPath::Load(m_IceCS.get(), m_Device, L"Shader/Water/WaterIceCS.hlsl")))
    {
        std::cout << "[Error] WaterSurface: WaterIceCS load failed" << std::endl;
        m_IceCS.reset();
        return false;
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = kIceSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;   // 型付き UAV を読み書きできる形式
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_IceTex))) return false;
    if (FAILED(m_Device->CreateShaderResourceView(m_IceTex.Get(), nullptr, &m_IceSRV))) return false;
    if (FAILED(m_Device->CreateUnorderedAccessView(m_IceTex.Get(), nullptr, &m_IceUAV))) return false;

    td.Width = td.Height = kIceProbeSize;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto& t : m_IceProbe)
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &t))) return false;
    ClearIce();
    return true;
}

// 池の水面の高さ（氷の texel の中心で）。池の外は負の大きな値 = 持ち上げない
void WaterSurface::BuildPondTop(const TerrainBuild::ReliefField& rf)
{
    m_PondTopTex.Reset();
    m_PondTopSRV.Reset();
    if (!m_Device) return;
    constexpr float kNoPond = -1000.0f;
    std::vector<float> top((size_t)kIceSize * kIceSize, kNoPond);
    if (rf.gw > 0 && rf.pond.size() == (size_t)rf.gw * rf.gd)
    {
        const float tx = m_MapSize.x / kIceSize, tz = m_MapSize.y / kIceSize;
        for (int v = 0; v < kIceSize; ++v)
        {
            const float z = m_MapOrigin.y + (v + 0.5f) * tz;
            const int gz = (int)std::floor((z - rf.oz) / TerrainBuild::kCs);
            if (gz < 0 || gz >= rf.gd) continue;
            for (int u = 0; u < kIceSize; ++u)
            {
                const float x = m_MapOrigin.x + (u + 0.5f) * tx;
                const int gx = (int)std::floor((x - rf.ox) / TerrainBuild::kCs);
                if (gx < 0 || gx >= rf.gw || !rf.pond[(size_t)gz * rf.gw + gx]) continue;
                top[(size_t)v * kIceSize + u] = rf.SurfaceIn(rf.ZoneAtCell(gx, gz), x, z) + TerrainBuild::kPondSurface;
            }
        }
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = kIceSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = top.data();
    sd.SysMemPitch = kIceSize * sizeof(float);
    if (SUCCEEDED(m_Device->CreateTexture2D(&td, &sd, &m_PondTopTex)))
        m_Device->CreateShaderResourceView(m_PondTopTex.Get(), nullptr, &m_PondTopSRV);
}

// プレイヤーの周りの氷を staging へ写し、一番古い物を読む（GPU を待たない）
void WaterSurface::ProbeIce()
{
    if (!m_IceProbe[0] || m_MapSize.x <= 0.0f || m_MapSize.y <= 0.0f) return;
    const int cu = (int)((m_IceProbePos.x - m_MapOrigin.x) / m_MapSize.x * (float)kIceSize);
    const int cv = (int)((m_IceProbePos.z - m_MapOrigin.y) / m_MapSize.y * (float)kIceSize);
    const int x0 = std::clamp(cu - kIceProbeSize / 2, 0, kIceSize - kIceProbeSize);
    const int y0 = std::clamp(cv - kIceProbeSize / 2, 0, kIceSize - kIceProbeSize);
    D3D11_BOX box = { (UINT)x0, (UINT)y0, 0, (UINT)(x0 + kIceProbeSize), (UINT)(y0 + kIceProbeSize), 1 };
    const int cur = m_IceProbeCur;
    m_Context->CopySubresourceRegion(m_IceProbe[cur].Get(), 0, 0, 0, 0, m_IceTex.Get(), 0, &box);
    m_IceProbeOrigin[cur][0] = x0;
    m_IceProbeOrigin[cur][1] = y0;
    m_IceProbeUsed[cur] = true;
    m_IceProbeCur = (cur + 1) % kIceProbeFrames;

    const int old = m_IceProbeCur;   // 次に上書きする = 一番古い
    if (!m_IceProbeUsed[old]) return;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_IceProbe[old].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) return;
    for (int y = 0; y < kIceProbeSize; ++y)
        memcpy(&m_IceCpu[y * kIceProbeSize], static_cast<const uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch, kIceProbeSize * sizeof(float));
    m_Context->Unmap(m_IceProbe[old].Get(), 0);
    m_IceCpuX = m_IceProbeOrigin[old][0];
    m_IceCpuZ = m_IceProbeOrigin[old][1];
    m_IceCpuValid = true;
}

bool WaterSurface::FrozenAt(float x, float z) const
{
    if (!m_IceCpuValid || m_MapSize.x <= 0.0f || m_MapSize.y <= 0.0f) return false;
    const int u = (int)((x - m_MapOrigin.x) / m_MapSize.x * (float)kIceSize) - m_IceCpuX;
    const int v = (int)((z - m_MapOrigin.y) / m_MapSize.y * (float)kIceSize) - m_IceCpuZ;
    if (u < 0 || v < 0 || u >= kIceProbeSize || v >= kIceProbeSize) return false;
    return m_IceCpu[v * kIceProbeSize + u] >= kIceSolid;
}

void WaterSurface::ClearIce()
{
    if (!m_Context || !m_IceUAV) return;
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_Context->ClearUnorderedAccessViewFloat(m_IceUAV.Get(), zero);
    m_IceCpuValid = false;
}

void WaterSurface::StepIce(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, float dt)
{
    if (!m_IceCS || !m_IceUAV || !m_MaskSRV || !m_HeatSRV || !areas || !areaStates) return;
    ID3D11DeviceContext* ctx = m_Context;
    IceCB cb = {};
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.dt = dt;
    cb.life = m_Settings.iceLife;
    m_IceCS->WriteBuffer(ctx, 0, &cb);
    m_IceCS->Bind(ctx);
    m_IceCS->SetSRV(ctx, "g_Mask", m_MaskSRV.Get());
    m_IceCS->SetSRV(ctx, "g_Areas", areas);
    m_IceCS->SetSRV(ctx, "g_AreaStates", areaStates);
    m_IceCS->SetSRV(ctx, "g_RecipeHeat", m_HeatSRV);
    m_IceCS->SetUAV(ctx, "g_Ice", m_IceUAV.Get());
    m_IceCS->BindUAVs(ctx);
    ctx->Dispatch((kIceSize + 7) / 8, (kIceSize + 7) / 8, 1);
    m_IceCS->UnbindSRVs(ctx);
    m_IceCS->UnbindUAVs(ctx);
    ProbeIce();
}

// TEMP-TEST: Map で GPU を待つので毎フレーム呼ばない
float WaterSurface::DebugIceCells() const
{
    if (!m_Context || !m_Device || !m_IceTex) return -1.0f;
    if (!m_IceStaging)
    {
        D3D11_TEXTURE2D_DESC td = {};
        m_IceTex->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_IceStaging))) return -1.0f;
    }
    m_Context->CopyResource(m_IceStaging.Get(), m_IceTex.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_IceStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return -1.0f;
    int cells = 0;
    for (int y = 0; y < kIceSize; ++y)
    {
        const float* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch);
        for (int x = 0; x < kIceSize; ++x)
            if (row[x] > 0.3f) ++cells;
    }
    m_Context->Unmap(m_IceStaging.Get(), 0);
    return (float)cells;
}
