// ============================================================
// GroundDebris.cpp
// ============================================================
#include "World/GroundDebris.h"
#include "World/GridWorld.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Light/PointLightManager.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Camera/CameraBase.h"

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

static_assert(sizeof(GroundDebris::DebrisCB) == 352, "DebrisCB layout mismatch");
static_assert(sizeof(GroundDebris::Debris) == 64, "Debris layout mismatch");

bool GroundDebris::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    // TEMP-TEST: 有無で見比べる
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_NO_DEBRIS", env, sizeof(env)) > 0)
        m_Settings.enabled = false;

    bool ok = true;
    m_UpdateCS = std::make_shared<ComputeShader>();
    if (FAILED(ShaderPath::Load(m_UpdateCS.get(), device, L"Shader/Debris/DebrisUpdateCS.hlsl"))) { m_UpdateCS.reset(); ok = false; }
    m_VS = std::make_shared<VertexShader>();
    if (FAILED(ShaderPath::Load(m_VS.get(), device, L"Shader/Debris/DebrisVS.hlsl"))) { m_VS.reset(); ok = false; }
    m_PS = std::make_shared<PixelShader>();
    if (FAILED(ShaderPath::Load(m_PS.get(), device, L"Shader/Debris/DebrisPS.hlsl"))) { m_PS.reset(); ok = false; }
    if (!ok)
    {
        std::cout << "[Error] GroundDebris: shader load failed" << std::endl;
        return false;
    }
    return CreateBuffer(m_Settings.side);
}

void GroundDebris::Shutdown()
{
    m_UpdateCS.reset();
    m_VS.reset();
    m_PS.reset();
    m_Buffer.Reset();
    m_UAV.Reset();
    m_SRV.Reset();
    m_HeightSRV.Reset();
    m_Gusts.clear();
    m_Ready = false;
    m_BufSide = 0;
}

bool GroundDebris::CreateBuffer(int side)
{
    side = std::clamp(side, 8, 256);
    m_Buffer.Reset();
    m_UAV.Reset();
    m_SRV.Reset();
    m_BufSide = 0;

    const UINT count = (UINT)side * (UINT)side;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = count * sizeof(Debris);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Debris);
    std::vector<Debris> zero(count);   // cell 0 / seed 0：最初の Step で全部置き直す（g_Reset も立てる）
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = zero.data();
    if (FAILED(m_Device->CreateBuffer(&bd, &sd, &m_Buffer))) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = count;
    if (FAILED(m_Device->CreateUnorderedAccessView(m_Buffer.Get(), &ud, &m_UAV))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_UNKNOWN;
    sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.NumElements = count;
    if (FAILED(m_Device->CreateShaderResourceView(m_Buffer.Get(), &sv, &m_SRV))) return false;

    m_BufSide = side;
    m_Settings.side = side;
    m_ResetPending = true;
    return true;
}

void GroundDebris::Build(const GridWorld& grid, uint32_t seed, TerrainGenerator::Biome biome, ID3D11ShaderResourceView* height)
{
    m_MapOrigin = Vector2(grid.OriginX(), grid.OriginZ());
    m_MapSize = Vector2(grid.WorldWidth(), grid.WorldDepth());
    m_Seed = seed;
    m_HeightSRV = height;
    m_Gusts.clear();
    m_ResetPending = true;
    m_Ready = (m_HeightSRV != nullptr) && m_Buffer;

    // 面ごとの色（草原は既定値）
    auto& s = m_Settings;
    switch (biome)
    {
    case TerrainGenerator::Biome::Desert:
        s.colorLeaf = { 0.78f, 0.66f, 0.42f };   // 乾いた砂色の破片
        s.colorClip = { 0.70f, 0.62f, 0.40f };   // 枯れ草
        s.colorTwig = { 0.46f, 0.34f, 0.20f };   // 枯枝
        break;
    case TerrainGenerator::Biome::Dungeon:
        s.colorLeaf = { 0.62f, 0.60f, 0.56f };   // 紙片
        s.colorClip = { 0.36f, 0.36f, 0.38f };   // 灰
        s.colorTwig = { 0.30f, 0.26f, 0.22f };   // 焦げた木片
        break;
    default:
        s.colorLeaf = { 0.64f, 0.50f, 0.20f };
        s.colorClip = { 0.56f, 0.64f, 0.24f };
        s.colorTwig = { 0.40f, 0.28f, 0.14f };
        break;
    }
}

void GroundDebris::AddGust(const Vector3& pos, float radius, const Vector2& dir, float length)
{
    if (m_Gusts.size() >= 16) m_Gusts.erase(m_Gusts.begin());
    m_Gusts.push_back({ pos, radius, dir, length, 0.0f });
}

void GroundDebris::Render(Renderer& renderer, CameraBase& camera,
    ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* areaEnds)
{
    ID3D11DeviceContext* ctx = m_Context;
    if (!Ready() || !ctx || !m_UpdateCS || !m_VS || !m_PS || !m_HeightSRV) return;

    auto& s = m_Settings;
    if (s.side != m_BufSide && !CreateBuffer(s.side)) return;

    const float dt = (std::min)(m_PendingDt, 0.1f);   // 一時停止中（Tick 無し）は 0 = 止まる
    m_PendingDt = 0.0f;
    m_Time += dt;

    // 突風：年齢を進め、消えた物を落とす
    for (auto& g : m_Gusts) g.age += dt;
    m_Gusts.erase(std::remove_if(m_Gusts.begin(), m_Gusts.end(),
        [&](const GustSrc& g) { return g.age >= s.gustTime; }), m_Gusts.end());

    const Vector3 cam = camera.GetPosition();
    DebrisCB cb = {};
    cb.viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
    cb.camPos = cam;
    cb.dt = dt;
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.windDir = m_Wind.dir;
    cb.windSpeed = m_Wind.speed * m_Wind.strength;
    cb.windGust = m_Wind.gust * m_Wind.strength;
    cb.windWaveScale = m_Wind.waveScale;
    cb.windWaveSpeed = m_Wind.waveSpeed;
    cb.windTime = m_WindTime;
    cb.window = (std::max)(s.window, 4.0f);
    cb.side = (uint32_t)m_BufSide;
    cb.spacing = 2.0f * cb.window / (float)m_BufSide;
    cb.liftWind = (std::max)(s.liftWind, 0.1f);
    cb.gravity = s.gravity;
    cb.drag = (std::max)(s.drag, 0.0f);
    cb.density = std::clamp(s.density, 0.0f, 1.0f);
    cb.fadeWidth = (std::max)(s.fadeWidth, 0.1f);
    cb.sizeMul = s.sizeMul;
    cb.colorLeaf = s.colorLeaf;
    cb.blastAccel = s.blastAccel;
    cb.colorClip = s.colorClip;
    cb.gustSpeed = s.gustSpeed;
    cb.colorTwig = s.colorTwig;
    cb.time = m_Time;
    uint32_t n = 0;
    for (const auto& g : m_Gusts)
    {
        if (n >= (uint32_t)kMaxGusts) break;
        cb.gustA[n] = Vector4(g.pos.x, g.pos.z, g.dir.x, g.dir.y);
        cb.gustB[n] = Vector4(g.length, g.radius, 1.0f - g.age / (std::max)(s.gustTime, 0.01f), 0.0f);
        ++n;
    }
    cb.gustCount = n;
    cb.seed = m_Seed;
    cb.reset = m_ResetPending ? 1u : 0u;
    cb.maxSlope = s.maxSlope;
    m_ResetPending = false;

    // ---- 1) 1 歩進める ----
    ID3D11SamplerState* linearClamp = RenderStates::Get().LinearClamp();
    m_UpdateCS->WriteBuffer(ctx, 0, &cb);
    m_UpdateCS->Bind(ctx);
    m_UpdateCS->SetSRV(ctx, "g_Height", m_HeightSRV.Get());
    m_UpdateCS->SetSRV(ctx, "g_Areas", areas);
    m_UpdateCS->SetSRV(ctx, "g_AreaStates", areaStates);
    m_UpdateCS->SetSRV(ctx, "g_AreaEnds", areaEnds);
    ctx->CSSetSamplers(0, 1, &linearClamp);
    m_UpdateCS->SetUAV(ctx, "g_Debris", m_UAV.Get());
    m_UpdateCS->BindUAVs(ctx);
    ctx->Dispatch((UINT)((m_BufSide * m_BufSide + 255) / 256), 1, 1);
    m_UpdateCS->UnbindSRVs(ctx);
    m_UpdateCS->UnbindUAVs(ctx);

    // ---- 2) 描く（頂点バッファ無し。1 個 = 板 6 頂点）----
    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = cam;
    m_VS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 1, &light);
    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    m_VS->SetSRV(ctx, "g_Debris", m_SRV.Get());
    PointLightManager::Get().BindPS(ctx);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(rs.DepthDefault(), 0);
    ctx->RSSetState(rs.CullNone());   // 板は両面

    ctx->DrawInstanced(6, (UINT)(m_BufSide * m_BufSide), 0, 0);

    // 次のフレームの CS が UAV にするので外す
    m_VS->UnbindSRVs(ctx);
    PointLightManager::Get().UnbindPS(ctx);
    rs.Restore(ctx);
}
