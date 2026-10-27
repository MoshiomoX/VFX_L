// ============================================================
// WaterSurface.cpp
// 水面のメッシュ・問い合わせ・描画（波紋の計算は WaterRipples.cpp、パネルは WaterSurfacePanel.cpp）
// ============================================================
#include "World/WaterSurface.h"
#include "World/TerrainBuild.h"
#include "World/MapData.h"
#include "World/GridWorld.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Camera/CameraBase.h"

using DirectX::SimpleMath::Matrix;
using Vector2 = WaterSurface::Vector2;
using Vector3 = WaterSurface::Vector3;
using Vector4 = WaterSurface::Vector4;

static_assert(sizeof(WaterSurface::WaterCB) == 288, "WaterCB layout mismatch");
static_assert(sizeof(WaterSurface::RippleCB) == 336, "WaterRippleCB layout mismatch");

bool WaterSurface::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    // TEMP-TEST: 有無で見比べる
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_NO_WATER", env, sizeof(env)) > 0)
        m_Settings.enabled = false;

    bool ok = true;
    m_VS = std::make_shared<VertexShader>();
    if (FAILED(ShaderPath::Load(m_VS.get(), device, L"Shader/Water/WaterVS.hlsl"))) { m_VS.reset(); ok = false; }
    m_PS = std::make_shared<PixelShader>();
    if (FAILED(ShaderPath::Load(m_PS.get(), device, L"Shader/Water/WaterPS.hlsl"))) { m_PS.reset(); ok = false; }
    m_RippleCS = std::make_shared<ComputeShader>();
    if (FAILED(ShaderPath::Load(m_RippleCS.get(), device, L"Shader/Water/WaterRippleCS.hlsl"))) { m_RippleCS.reset(); ok = false; }
    if (!ok)
    {
        std::cout << "[Error] WaterSurface: shader load failed" << std::endl;
        return false;
    }
    return CreateRippleTargets();
}

void WaterSurface::Shutdown()
{
    m_VS.reset();
    m_PS.reset();
    m_RippleCS.reset();
    m_VertBuffer.Reset();
    m_VertSRV.Reset();
    for (int i = 0; i < 2; ++i) { m_RippleTex[i].Reset(); m_RippleSRV[i].Reset(); m_RippleUAV[i].Reset(); }
    m_MaskTex.Reset();
    m_MaskSRV.Reset();
    m_Staging.Reset();
    m_Relief.reset();
    m_Impulses.clear();
    m_VertexCount = 0;
    m_PondCells = 0;
    m_Device = nullptr;
    m_Context = nullptr;
}

// ============================================================
// 地形の後：水たまりのマスからメッシュ（4×4 の四角 / マス）と波紋のマスクを作る
// ============================================================
void WaterSurface::Build(const GridWorld& grid, const MapData::Map& map, TerrainGenerator::Biome biome)
{
    using TerrainBuild::kCs;
    m_VertBuffer.Reset();
    m_VertSRV.Reset();
    m_VertexCount = 0;
    m_PondCells = 0;
    m_Impulses.clear();
    m_Relief.reset();
    m_MapOrigin = Vector2(grid.OriginX(), grid.OriginZ());
    m_MapSize = Vector2(grid.WorldWidth(), grid.WorldDepth());

    // 面ごとの色（草原は既定値）
    auto& s = m_Settings;
    switch (biome)
    {
    case TerrainGenerator::Biome::Desert:    // オアシス：明るい青緑
        s.shallowColor = { 0.14f, 0.50f, 0.44f };
        s.deepColor = { 0.03f, 0.20f, 0.28f };
        s.foamColor = { 0.95f, 0.96f, 0.90f };
        s.turbidColor = { 0.50f, 0.38f, 0.20f };
        break;
    case TerrainGenerator::Biome::Dungeon:   // 遺跡の溜まり水：暗く、泡も控えめ
        s.shallowColor = { 0.06f, 0.16f, 0.18f };
        s.deepColor = { 0.012f, 0.035f, 0.06f };
        s.foamColor = { 0.50f, 0.56f, 0.58f };
        s.turbidColor = { 0.20f, 0.18f, 0.14f };
        break;
    default:                                 // 草原の池
        s.shallowColor = { 0.10f, 0.42f, 0.46f };
        s.deepColor = { 0.03f, 0.14f, 0.30f };
        s.foamColor = { 0.92f, 0.96f, 1.00f };
        s.turbidColor = { 0.46f, 0.36f, 0.20f };
        break;
    }

    const int gw = grid.Width(), gd = grid.Depth();
    const bool hasPond = map.gw == gw && map.gd == gd && map.pond.size() == (size_t)gw * gd && TerrainBuild::AnyPond(map.pond);
    if (!hasPond || !m_Device)
    {
        BuildMask(TerrainBuild::ReliefField{});   // 全部 0（CS は何も動かさない）
        ClearRipples();
        return;
    }
    m_Relief = TerrainBuild::ReliefFromMap(grid, map);
    const TerrainBuild::ReliefField& rf = *m_Relief;

    auto isPond = [&](int x, int z) { return x >= 0 && z >= 0 && x < gw && z < gd && rf.pond[(size_t)z * gw + x] != 0; };
    // 岸からの距離 = 一番近い「水たまりでないマス」の四角までの距離（±R マスまで探す）
    constexpr int R = 3;
    const float maxDist = R * kCs;
    auto shoreDist = [&](float px, float pz)
        {
            const int cx = (int)std::floor((px - rf.ox) / kCs), cz = (int)std::floor((pz - rf.oz) / kCs);
            float best = maxDist;
            for (int dz = -R; dz <= R; ++dz)
                for (int dx = -R; dx <= R; ++dx)
                {
                    const int gx = cx + dx, gz = cz + dz;
                    if (isPond(gx, gz)) continue;
                    const float x0 = rf.ox + gx * kCs, z0 = rf.oz + gz * kCs;
                    const float ex = (std::max)((std::max)(x0 - px, px - (x0 + kCs)), 0.0f);
                    const float ez = (std::max)((std::max)(z0 - pz, pz - (z0 + kCs)), 0.0f);
                    best = (std::min)(best, std::sqrt(ex * ex + ez * ez));
                }
            return best;
        };

    constexpr int kSub = 4;   // 0.5m の四角
    const float step = kCs / kSub;
    std::vector<Vector4> verts;
    for (int gz = 0; gz < gd; ++gz)
        for (int gx = 0; gx < gw; ++gx)
        {
            if (!isPond(gx, gz)) continue;
            ++m_PondCells;
            const uint8_t k = rf.ZoneAtCell(gx, gz);
            Vector4 node[kSub + 1][kSub + 1];
            for (int j = 0; j <= kSub; ++j)
                for (int i = 0; i <= kSub; ++i)
                {
                    const float x = rf.ox + gx * kCs + i * step, z = rf.oz + gz * kCs + j * step;
                    node[j][i] = Vector4(x, rf.SurfaceIn(k, x, z) + TerrainBuild::kPondSurface, z, shoreDist(x, z));
                }
            for (int j = 0; j < kSub; ++j)
                for (int i = 0; i < kSub; ++i)
                {
                    const Vector4& a = node[j][i];
                    const Vector4& b = node[j][i + 1];
                    const Vector4& c = node[j + 1][i + 1];
                    const Vector4& d = node[j + 1][i];
                    verts.insert(verts.end(), { a, c, b, a, d, c });
                }
        }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(verts.size() * sizeof(Vector4));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Vector4);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = verts.data();
    if (SUCCEEDED(m_Device->CreateBuffer(&bd, &sd, &m_VertBuffer)))
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
        vd.Format = DXGI_FORMAT_UNKNOWN;
        vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        vd.Buffer.NumElements = (UINT)verts.size();
        if (SUCCEEDED(m_Device->CreateShaderResourceView(m_VertBuffer.Get(), &vd, &m_VertSRV)))
            m_VertexCount = (uint32_t)verts.size();
    }
    BuildMask(rf);
    ClearRipples();
    std::cout << "[Water] " << m_PondCells << " pond cells, " << m_VertexCount / 3 << " triangles" << std::endl;
}

bool WaterSurface::WaterAt(float x, float z, float& surfaceY) const
{
    if (!m_Relief || !HasWater()) return false;
    const TerrainBuild::ReliefField& rf = *m_Relief;
    const int gx = (int)std::floor((x - rf.ox) / TerrainBuild::kCs), gz = (int)std::floor((z - rf.oz) / TerrainBuild::kCs);
    if (gx < 0 || gz < 0 || gx >= rf.gw || gz >= rf.gd || !rf.pond[(size_t)gz * rf.gw + gx]) return false;
    surfaceY = rf.SurfaceIn(rf.ZoneAtCell(gx, gz), x, z) + TerrainBuild::kPondSurface;
    return true;
}

void WaterSurface::AddRipple(const Vector3& pos, float radius, float strength)
{
    if (!HasWater() || strength == 0.0f || radius <= 0.0f) return;
    float surf;
    if (!WaterAt(pos.x, pos.z, surf)) return;   // 水の外の分は CS のマスクが捨てるが、枠を使わないように
    const Vector4 v(pos.x, pos.z, radius, strength);
    if ((int)m_Impulses.size() < kMaxImpulses) { m_Impulses.push_back(v); return; }
    // 一杯なら一番弱い物と入れ替える
    auto weakest = std::min_element(m_Impulses.begin(), m_Impulses.end(),
        [](const Vector4& a, const Vector4& b) { return std::abs(a.w) < std::abs(b.w); });
    if (std::abs(weakest->w) < std::abs(strength)) *weakest = v;
}

// ============================================================
// 波紋を進めて描く
// ============================================================
void WaterSurface::Render(Renderer& renderer, CameraBase& camera,
    ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* windSources)
{
    ID3D11DeviceContext* ctx = m_Context;
    const float dt = (std::min)(m_PendingDt, 0.1f);   // 一時停止中（Tick 無し）は 0 = 波も止まる
    m_PendingDt = 0.0f;
    m_Time += dt;
    if (!ctx || !Ready())
    {
        m_Impulses.clear();
        return;
    }
    m_SimAccum += dt;
    StepRipples(areas, areaStates);

    auto& s = m_Settings;
    const Vector3 cam = camera.GetPosition();
    WaterCB cb = {};
    cb.viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
    cb.camPos = cam;
    cb.time = m_Time;
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.windDir = m_Wind.dir;
    cb.windSpeed = m_Wind.speed * m_Wind.strength;
    cb.windGust = m_Wind.gust * m_Wind.strength;
    cb.windScale = m_Wind.waveScale;
    cb.windWaveSpeed = m_Wind.waveSpeed;
    cb.windTime = m_WindTime;
    cb.wave2Weight = m_Wave2Weight;
    cb.wave2 = m_Wave2;
    cb.shallow = Vector4(s.shallowColor.x, s.shallowColor.y, s.shallowColor.z, std::clamp(s.shallowAlpha, 0.0f, 1.0f));
    cb.deep = Vector4(s.deepColor.x, s.deepColor.y, s.deepColor.z, std::clamp(s.deepAlpha, 0.0f, 1.0f));
    cb.foam = Vector4(s.foamColor.x, s.foamColor.y, s.foamColor.z, (std::max)(s.foamWidth, 0.0f));
    cb.turbid = Vector4(s.turbidColor.x, s.turbidColor.y, s.turbidColor.z, std::clamp(m_Turbid, 0.0f, 1.0f));
    cb.deepDist = (std::max)(s.deepDist, 0.05f);
    cb.calmWind = s.calmWind;
    cb.fullWind = (std::max)(s.fullWind, s.calmWind + 0.1f);
    cb.waveNormal = s.waveNormal;
    cb.waveScale = s.waveScale;
    cb.waveFlow = s.waveFlow;
    cb.rippleNormal = s.rippleNormal;
    cb.rippleCrest = (std::max)(s.rippleCrest, 1e-4f);
    cb.skyReflect = std::clamp(s.skyReflect, 0.0f, 1.0f);
    cb.specStrength = s.specStrength;
    cb.windDarken = std::clamp(s.windDarken, 0.0f, 1.0f);
    cb.hasSources = windSources ? 1.0f : 0.0f;
    cb.skyZenith = Vector4(m_SkyZenith.x, m_SkyZenith.y, m_SkyZenith.z, 0.0f);
    cb.skyHorizon = Vector4(m_SkyHorizon.x, m_SkyHorizon.y, m_SkyHorizon.z, 0.0f);

    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = cam;
    m_VS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 1, &light);
    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    m_VS->SetSRV(ctx, "g_Verts", m_VertSRV.Get());
    m_PS->SetSRV(ctx, "g_Ripple", m_RippleSRV[m_Cur].Get());
    if (windSources) m_PS->SetSRV(ctx, "g_WindSources", windSources);
    ID3D11SamplerState* linearClamp = RenderStates::Get().LinearClamp();
    ctx->PSSetSamplers(0, 1, &linearClamp);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);   // 前乗算アルファ
    ctx->OMSetDepthStencilState(rs.DepthReadOnly(), 0);               // 水の下の草・粒子・範囲の印を消さない
    ctx->RSSetState(rs.CullNone());

    ctx->Draw(m_VertexCount, 0);

    // 次のフレームの CS が波紋を UAV にするので外す
    m_VS->UnbindSRVs(ctx);
    m_PS->UnbindSRVs(ctx);
    rs.Restore(ctx);
}
