// ============================================================
// WaterRipples.cpp
// WaterSurface の波紋：フィールド全体の波動方程式（Shader/Water/WaterRippleCS.hlsl）を固定の 1/60 秒で進める。
//   テクスチャ = R16G16B16A16F（x = 今の高さ m、y = 1 歩前の高さ、zw = 白い線を描く大きな波（爆発・Boss）だけの
//   同じ物。波は重ね合わせなので zw = 全体の中のその波の分）を 2 枚交互。水たまりの外（マスクが 0）は常に 0。
//   波紋を起こす物は 1 フレームの最初の 1 歩だけで足す：CPU の押し（足・着地。AddRipple）、
//   GPU の範囲が出た瞬間（枠ごとに前に見た残り時間を覚えておき、空だった / 増えた = 新しい。
//   kAreaShake = 爆発は強く・白い線の出る大きな波、他は弱く）、雨粒（CS の乱数）
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
    // 1 歩ごとに高さを 0 へ戻す割合。押しは水の量を変えない（WaterRippleCS の Splash）ので丸め誤差を消すだけ。
    // 以前の 0.004 は「押し下げるだけの押し」で池全体が沈むのを戻すためだったが、長い波（2m 以上）がその場で
    // 上下するだけで広がらず、波紋が輪にならなかった（2026-10-28）
    constexpr float kRestore = 0.0003f;
}

bool WaterSurface::CreateRippleTargets()
{
    for (int i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = kRippleSize;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_RippleTex[i]))) return false;
        if (FAILED(m_Device->CreateShaderResourceView(m_RippleTex[i].Get(), nullptr, &m_RippleSRV[i]))) return false;
        if (FAILED(m_Device->CreateUnorderedAccessView(m_RippleTex[i].Get(), nullptr, &m_RippleUAV[i]))) return false;
    }
    // GPU の範囲の枠ごとの「前に見た時の残り時間」（WaterRippleCS の g_SeenIn / g_SeenOut）
    for (int i = 0; i < 2; ++i)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = kAreaSlots * sizeof(float);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &m_SeenBuf[i]))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R32_FLOAT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sv.Buffer.NumElements = kAreaSlots;
        if (FAILED(m_Device->CreateShaderResourceView(m_SeenBuf[i].Get(), &sv, &m_SeenSRV[i]))) return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = DXGI_FORMAT_R32_FLOAT;
        uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uv.Buffer.NumElements = kAreaSlots;
        if (FAILED(m_Device->CreateUnorderedAccessView(m_SeenBuf[i].Get(), &uv, &m_SeenUAV[i]))) return false;
    }
    // 水の中を歩く雑魚の押しの列（WaterStirCS → WaterRippleCS の g_Stirs / g_StirCount）
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = kMaxStirs * sizeof(Vector4);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(Vector4);
        if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &m_StirBuf))) return false;
        if (FAILED(m_Device->CreateShaderResourceView(m_StirBuf.Get(), nullptr, &m_StirSRV))) return false;
        if (FAILED(m_Device->CreateUnorderedAccessView(m_StirBuf.Get(), nullptr, &m_StirUAV))) return false;

        D3D11_BUFFER_DESC cd = {};
        cd.ByteWidth = sizeof(uint32_t);
        cd.Usage = D3D11_USAGE_DEFAULT;
        cd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(m_Device->CreateBuffer(&cd, nullptr, &m_StirCountBuf))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R32_UINT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sv.Buffer.NumElements = 1;
        if (FAILED(m_Device->CreateShaderResourceView(m_StirCountBuf.Get(), &sv, &m_StirCountSRV))) return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = DXGI_FORMAT_R32_UINT;
        uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uv.Buffer.NumElements = 1;
        if (FAILED(m_Device->CreateUnorderedAccessView(m_StirCountBuf.Get(), &uv, &m_StirCountUAV))) return false;
    }
    ClearRipples();
    return true;
}

void WaterSurface::ClearRipples()
{
    if (!m_Context) return;
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 2; ++i)
    {
        if (m_RippleUAV[i]) m_Context->ClearUnorderedAccessViewFloat(m_RippleUAV[i].Get(), zero);
        if (m_SeenUAV[i]) m_Context->ClearUnorderedAccessViewFloat(m_SeenUAV[i].Get(), zero);
    }
    const UINT zeroU[4] = { 0, 0, 0, 0 };
    if (m_StirCountUAV) m_Context->ClearUnorderedAccessViewUint(m_StirCountUAV.Get(), zeroU);
    m_StirDt = 0.0f;
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

// 水の中を歩く雑魚の押しを集める（WaterStirCS、2026-10-28）。波が進むフレームの最初の 1 歩の前に 1 回。
// 押す強さは前に集めてからの秒に比例（波が進まなかったフレームの分も入る）
void WaterSurface::GatherStirs()
{
    ID3D11DeviceContext* ctx = m_Context;
    const UINT zeroU[4] = { 0, 0, 0, 0 };
    if (m_StirCountUAV) ctx->ClearUnorderedAccessViewUint(m_StirCountUAV.Get(), zeroU);
    const float dt = (std::min)(m_StirDt, 0.1f);
    m_StirDt = 0.0f;
    const auto& s = m_Settings;
    if (!s.mobWake || !m_StirCS || !m_StirUAV || !m_EnemySRV || !m_EnemyStateSRV || !m_EnemyExtraSRV || dt <= 0.0f) return;

    StirCB cb = {};
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.strength = s.mobWakeStrength;
    cb.radius = (std::max)(s.mobWakeRadius, 0.05f);
    cb.minSpeed = s.mobWakeMinSpeed;
    cb.dt = dt;
    cb.maxStirs = kMaxStirs;
    cb.hasHeight = m_GroundSRV ? 1u : 0u;
    cb.stepStrength = s.mobStepStrength;
    cb.stride = (std::max)(s.mobStride, 0.1f);
    cb.time = m_Time;
    cb.groundY = m_EnemyGroundY;
    m_StirCS->WriteBuffer(ctx, 0, &cb);
    m_StirCS->Bind(ctx);
    m_StirCS->SetSRV(ctx, "g_Enemies", m_EnemySRV);
    m_StirCS->SetSRV(ctx, "g_EnemyStates", m_EnemyStateSRV);
    m_StirCS->SetSRV(ctx, "g_EnemyExtra", m_EnemyExtraSRV);
    m_StirCS->SetSRV(ctx, "g_Mask", m_MaskSRV.Get());
    m_StirCS->SetSRV(ctx, "g_Height", m_GroundSRV);
    m_StirCS->SetUAV(ctx, "g_Stirs", m_StirUAV.Get());
    m_StirCS->SetUAV(ctx, "g_StirCount", m_StirCountUAV.Get());
    m_StirCS->BindUAVs(ctx);
    ctx->Dispatch((kEnemyThreads + 255) / 256, 1, 1);
    m_StirCS->UnbindSRVs(ctx);
    m_StirCS->UnbindUAVs(ctx);
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

    GatherStirs();   // 水の中を歩く雑魚（最初の 1 歩で使う）

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
        m_RippleCS->SetSRV(ctx, "g_SeenIn", m_SeenSRV[m_SeenCur].Get());
        m_RippleCS->SetSRV(ctx, "g_Stirs", m_StirSRV.Get());
        m_RippleCS->SetSRV(ctx, "g_StirCount", m_StirCountSRV.Get());
        m_RippleCS->SetUAV(ctx, "g_Out", m_RippleUAV[1 - m_Cur].Get());
        m_RippleCS->SetUAV(ctx, "g_SeenOut", m_SeenUAV[1 - m_SeenCur].Get());   // 最初の 1 歩だけ書く
        m_RippleCS->BindUAVs(ctx);
        ctx->Dispatch((kRippleSize + 7) / 8, (kRippleSize + 7) / 8, 1);
        m_RippleCS->UnbindSRVs(ctx);
        m_RippleCS->UnbindUAVs(ctx);
        m_Cur = 1 - m_Cur;
    }
    m_SeenCur = 1 - m_SeenCur;
    m_Impulses.clear();
}

// TEMP-TEST: 自動テストの検証用。Map で GPU を待つので毎フレーム呼ばない
bool WaterSurface::DebugReadRipples(float& maxAbs, uint32_t& moving, float* maxUp, float* maxCrest) const
{
    maxAbs = 0.0f;
    if (maxUp) *maxUp = 0.0f;
    if (maxCrest) *maxCrest = 0.0f;
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
            const float hs = DirectX::PackedVector::XMConvertHalfToFloat(row[u * 4]);
            if (maxUp) *maxUp = (std::max)(*maxUp, hs);
            if (maxCrest) *maxCrest = (std::max)(*maxCrest, DirectX::PackedVector::XMConvertHalfToFloat(row[u * 4 + 2]));
            const float h = std::abs(hs);
            maxAbs = (std::max)(maxAbs, h);
            if (h > threshold) ++moving;
        }
    }
    m_Context->Unmap(m_Staging.Get(), 0);
    return true;
}

// TEMP-TEST: 波紋の中身を画像で見る（自動テストだけ）
bool WaterSurface::DebugDumpRipples(const char* path, float cx, float cz, float half) const
{
    float maxAbs; uint32_t moving;
    if (!DebugReadRipples(maxAbs, moving)) return false;   // m_Staging に今の波紋を写す
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const float texel = m_MapSize.x / kRippleSize;
    const int n = (std::max)(1, (int)(half * 2.0f / texel));
    const int u0 = (int)((cx - half - m_MapOrigin.x) / texel), v0 = (int)((cz - half - m_MapOrigin.y) / texel);
    std::vector<uint8_t> img((size_t)n * 2 * n, 0);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const int u = u0 + x, v = v0 + (n - 1 - y);
            if (u < 0 || v < 0 || u >= kRippleSize || v >= kRippleSize) continue;
            const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + (size_t)v * mapped.RowPitch);
            for (int c = 0; c < 2; ++c)
            {
                const float h = DirectX::PackedVector::XMConvertHalfToFloat(row[u * 4 + c * 2]);
                img[(size_t)y * 2 * n + (size_t)c * n + x] = (uint8_t)std::clamp(128.0f + h / 0.05f * 127.0f, 0.0f, 255.0f);
            }
        }
    m_Context->Unmap(m_Staging.Get(), 0);
    std::ofstream f(path, std::ios::binary);
    f << "P5\n" << n * 2 << " " << n << "\n255\n";
    f.write(reinterpret_cast<const char*>(img.data()), (std::streamsize)img.size());
    return true;
}
