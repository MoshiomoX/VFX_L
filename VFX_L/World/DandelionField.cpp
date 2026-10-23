// ============================================================
// DandelionField.cpp
// ============================================================
#include "World/DandelionField.h"
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

static_assert(sizeof(DandelionField::DandelionCB) == 304, "DandelionCB layout mismatch");
static_assert(sizeof(DandelionField::Flower) == 48, "Flower layout mismatch");
static_assert(sizeof(DandelionField::Seed) == 32, "Seed layout mismatch");

bool DandelionField::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    // TEMP-TEST: 既定は切（2026-10-23）。VFXL_DANDELIONS=1 で入れる
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_DANDELIONS", env, sizeof(env)) > 0 && env[0] == '1')
        m_Settings.enabled = true;

    bool ok = true;
    m_UpdateCS = std::make_shared<ComputeShader>();
    if (FAILED(ShaderPath::Load(m_UpdateCS.get(), device, L"Shader/Dandelion/DandelionUpdateCS.hlsl"))) { m_UpdateCS.reset(); ok = false; }
    m_VS = std::make_shared<VertexShader>();
    if (FAILED(ShaderPath::Load(m_VS.get(), device, L"Shader/Dandelion/DandelionVS.hlsl"))) { m_VS.reset(); ok = false; }
    m_PS = std::make_shared<PixelShader>();
    if (FAILED(ShaderPath::Load(m_PS.get(), device, L"Shader/Dandelion/DandelionPS.hlsl"))) { m_PS.reset(); ok = false; }
    if (!ok)
    {
        std::cout << "[Error] DandelionField: shader load failed" << std::endl;
        return false;
    }
    return CreateBuffers(m_Settings.side, m_Settings.seedsPer);
}

void DandelionField::Shutdown()
{
    m_UpdateCS.reset();
    m_VS.reset();
    m_PS.reset();
    m_FlowerBuffer.Reset(); m_FlowerUAV.Reset(); m_FlowerSRV.Reset();
    m_SeedBuffer.Reset(); m_SeedUAV.Reset(); m_SeedSRV.Reset();
    m_HeightSRV.Reset();
    m_GroundSRV.Reset();
    m_Ready = false;
    m_BufSide = m_BufSeeds = 0;
}

namespace
{
    bool MakeStructured(ID3D11Device* device, UINT stride, UINT count,
        Microsoft::WRL::ComPtr<ID3D11Buffer>& buf,
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& uav,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = stride * count;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = stride;
        std::vector<uint8_t> zero((size_t)stride * count, 0);   // cell 0 / life 0：最初の Step で全部置き直す
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = zero.data();
        if (FAILED(device->CreateBuffer(&bd, &sd, &buf))) return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = count;
        if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_UNKNOWN;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sv.Buffer.NumElements = count;
        return SUCCEEDED(device->CreateShaderResourceView(buf.Get(), &sv, &srv));
    }
}

bool DandelionField::CreateBuffers(int side, int seedsPer)
{
    side = std::clamp(side, 8, 192);
    seedsPer = std::clamp(seedsPer, 1, 24);
    m_FlowerBuffer.Reset(); m_FlowerUAV.Reset(); m_FlowerSRV.Reset();
    m_SeedBuffer.Reset(); m_SeedUAV.Reset(); m_SeedSRV.Reset();
    m_BufSide = m_BufSeeds = 0;
    const UINT flowers = (UINT)side * (UINT)side;
    if (!MakeStructured(m_Device, sizeof(Flower), flowers, m_FlowerBuffer, m_FlowerUAV, m_FlowerSRV)) return false;
    if (!MakeStructured(m_Device, sizeof(Seed), flowers * (UINT)seedsPer, m_SeedBuffer, m_SeedUAV, m_SeedSRV)) return false;
    m_BufSide = side;
    m_BufSeeds = seedsPer;
    m_Settings.side = side;
    m_Settings.seedsPer = seedsPer;
    m_ResetPending = true;
    return true;
}

void DandelionField::Build(const GridWorld& grid, uint32_t seed, TerrainGenerator::Biome biome,
    ID3D11ShaderResourceView* height, ID3D11ShaderResourceView* ground)
{
    m_MapOrigin = Vector2(grid.OriginX(), grid.OriginZ());
    m_MapSize = Vector2(grid.WorldWidth(), grid.WorldDepth());
    m_Seed = seed;
    m_HeightSRV = height;
    m_GroundSRV = ground;
    m_Active = (biome == TerrainGenerator::Biome::Grassland);
    m_ResetPending = true;
    m_Ready = (m_HeightSRV != nullptr) && (m_GroundSRV != nullptr) && m_FlowerBuffer && m_SeedBuffer;
}

void DandelionField::Render(Renderer& renderer, CameraBase& camera,
    ID3D11ShaderResourceView* trample, ID3D11ShaderResourceView* windSources, const Vector3* player)
{
    ID3D11DeviceContext* ctx = m_Context;
    if (!Ready() || !ctx || !m_UpdateCS || !m_VS || !m_PS) return;

    auto& s = m_Settings;
    if ((s.side != m_BufSide || s.seedsPer != m_BufSeeds) && !CreateBuffers(s.side, s.seedsPer)) return;

    const float dt = (std::min)(m_PendingDt, 0.1f);   // 一時停止中（Tick 無し）は 0 = 止まる
    m_PendingDt = 0.0f;
    m_Time += dt;

    const Matrix view = camera.GetViewMatrix();
    DandelionCB cb = {};
    cb.viewProj = view * camera.GetProjectionMatrix();
    cb.camPos = camera.GetPosition();
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
    cb.seedsPer = (uint32_t)m_BufSeeds;
    cb.time = m_Time;
    cb.density = std::clamp(s.density, 0.0f, 1.0f);
    cb.patchScale = (std::max)(s.patchScale, 1.0f);
    cb.patchCover = std::clamp(s.patchCover, 0.0f, 1.0f);
    cb.fadeWidth = (std::max)(s.fadeWidth, 0.1f);
    cb.stemMin = s.stemMin;
    cb.stemMax = (std::max)(s.stemMax, s.stemMin);
    cb.puffRadius = s.puffRadius;
    cb.seedSize = s.seedSize;
    cb.releaseWind = (std::max)(s.releaseWind, 0.1f);
    cb.releaseLocal = (std::max)(s.releaseLocal, 0.1f);
    cb.releaseTrample = (std::max)(s.releaseTrample, 0.01f);
    cb.regrowDelay = s.regrowDelay;
    cb.regrowTime = s.regrowTime;
    cb.seedDrag = (std::max)(s.seedDrag, 0.0f);
    cb.seedGravity = s.seedGravity;
    cb.seedLifeMin = s.seedLifeMin;
    cb.seedLifeMax = (std::max)(s.seedLifeMax, s.seedLifeMin);
    cb.seedLift = s.seedLift;
    cb.bendGain = s.bendGain;
    cb.maxSlope = s.maxSlope;
    cb.stemColor = s.stemColor;
    cb.seed = m_Seed;
    cb.puffColor = s.puffColor;
    cb.reset = m_ResetPending ? 1u : 0u;
    cb.camRight = Vector3(view._11, view._21, view._31);
    cb.sizeMul = s.sizeMul;
    cb.camUp = Vector3(view._12, view._22, view._32);
    cb.playerPos = player ? *player : Vector3::Zero;
    cb.playerRadius = player ? (std::max)(s.playerRadius, 0.0f) : 0.0f;
    m_ResetPending = false;

    // ---- 1) 1 歩進める（踏み跡が無い時は 0 のまま = 踏んでもほどけない）----
    ID3D11SamplerState* linearClamp = RenderStates::Get().LinearClamp();
    m_UpdateCS->WriteBuffer(ctx, 0, &cb);
    m_UpdateCS->Bind(ctx);
    m_UpdateCS->SetSRV(ctx, "g_Height", m_HeightSRV.Get());
    m_UpdateCS->SetSRV(ctx, "g_Ground", m_GroundSRV.Get());
    if (trample) m_UpdateCS->SetSRV(ctx, "g_Trample", trample);
    if (windSources) m_UpdateCS->SetSRV(ctx, "g_WindSources", windSources);
    ctx->CSSetSamplers(0, 1, &linearClamp);
    m_UpdateCS->SetUAV(ctx, "g_Flowers", m_FlowerUAV.Get());
    m_UpdateCS->SetUAV(ctx, "g_Seeds", m_SeedUAV.Get());
    m_UpdateCS->BindUAVs(ctx);
    ctx->Dispatch((UINT)((m_BufSide * m_BufSide + 63) / 64), 1, 1);
    m_UpdateCS->UnbindSRVs(ctx);
    m_UpdateCS->UnbindUAVs(ctx);

    // ---- 2) 描く（頂点バッファ無し。1 本 = 茎 + 玉 + 綿毛 seedsPer 枚、各 6 頂点）----
    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = cb.camPos;
    m_VS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 1, &light);
    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    m_VS->SetSRV(ctx, "g_Flowers", m_FlowerSRV.Get());
    m_VS->SetSRV(ctx, "g_Seeds", m_SeedSRV.Get());
    PointLightManager::Get().BindPS(ctx);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(rs.DepthDefault(), 0);
    ctx->RSSetState(rs.CullNone());

    ctx->DrawInstanced(6, (UINT)(m_BufSide * m_BufSide * (2 + m_BufSeeds)), 0, 0);

    m_VS->UnbindSRVs(ctx);
    PointLightManager::Get().UnbindPS(ctx);
    rs.Restore(ctx);
}

// TEMP-TEST：花と綿毛を読み戻して数える（staging を作って Map で待つ。自動テストからだけ）
bool DandelionField::DebugStats(int& full, int& bare, int& flying, const Vector3* nearCenter, float nearRadius, int* nearCount)
{
    full = bare = flying = 0;
    if (nearCount) *nearCount = 0;
    if (!Ready()) return true;   // この面には生えていない（砂漠・遺跡）
    if (!m_Device || !m_Context || !m_FlowerBuffer || !m_SeedBuffer) return false;
    auto readBack = [&](ID3D11Buffer* src, std::vector<uint8_t>& out) -> bool
        {
            D3D11_BUFFER_DESC bd = {};
            src->GetDesc(&bd);
            bd.Usage = D3D11_USAGE_STAGING;
            bd.BindFlags = 0;
            bd.MiscFlags = 0;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
            if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &staging))) return false;
            m_Context->CopyResource(staging.Get(), src);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(m_Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
            out.resize(bd.ByteWidth);
            memcpy(out.data(), mapped.pData, bd.ByteWidth);
            m_Context->Unmap(staging.Get(), 0);
            return true;
        };
    std::vector<uint8_t> fb, sb;
    if (!readBack(m_FlowerBuffer.Get(), fb) || !readBack(m_SeedBuffer.Get(), sb)) return false;
    const Flower* fl = reinterpret_cast<const Flower*>(fb.data());
    const size_t nf = fb.size() / sizeof(Flower);
    for (size_t i = 0; i < nf; ++i)
    {
        if (fl[i].seed & 0x80000000u) continue;
        if (fl[i].puff > 0.95f) ++full;
        else ++bare;
        if (nearCenter && nearCount)
        {
            const float dx = fl[i].pos.x - nearCenter->x, dz = fl[i].pos.z - nearCenter->z;
            if (dx * dx + dz * dz < nearRadius * nearRadius) ++*nearCount;
        }
    }
    const Seed* se = reinterpret_cast<const Seed*>(sb.data());
    const size_t ns = sb.size() / sizeof(Seed);
    for (size_t i = 0; i < ns; ++i)
        if (se[i].life > 0.0f) ++flying;
    return true;
}
