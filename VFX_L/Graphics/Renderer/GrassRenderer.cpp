// ============================================================
// GrassRenderer.cpp
// ============================================================
#include "Graphics/Renderer/GrassRenderer.h"
#include "Graphics/Renderer/FrustumPlanes.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Light/PointLightManager.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "World/GridWorld.h"
#include "World/TerrainGenerator.h"
#include "Camera/CameraBase.h"
#include "imgui.h"

#include <DirectXPackedVector.h>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

static_assert(sizeof(GrassRenderer::GrassCB) == 384, "GrassCB layout mismatch");
static_assert(sizeof(GrassRenderer::GrassBlade) == 32, "GrassBlade layout mismatch");

bool GrassRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    // TEMP-TEST: 草の有無で負荷を比べる
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_NO_GRASS", env, sizeof(env)) > 0)
        m_Settings.enabled = false;

    bool ok = true;
    auto loadCS = [&](std::shared_ptr<ComputeShader>& cs, const wchar_t* path)
        {
            cs = std::make_shared<ComputeShader>();
            if (FAILED(ShaderPath::Load(cs.get(), device, path))) { cs.reset(); ok = false; }
        };
    loadCS(m_CullCS, L"Shader/Grass/GrassCullCS.hlsl");
    loadCS(m_TrampleCS, L"Shader/Grass/GrassTrampleCS.hlsl");
    loadCS(m_StampCS, L"Shader/Grass/GrassStampCS.hlsl");
    m_VS = std::make_shared<VertexShader>();
    if (FAILED(ShaderPath::Load(m_VS.get(), device, L"Shader/Grass/GrassVS.hlsl"))) { m_VS.reset(); ok = false; }
    m_PS = std::make_shared<PixelShader>();
    if (FAILED(ShaderPath::Load(m_PS.get(), device, L"Shader/Grass/GrassPS.hlsl"))) { m_PS.reset(); ok = false; }
    if (!ok)
    {
        std::cout << "[Error] GrassRenderer: shader load failed" << std::endl;
        return false;
    }
    return CreateBuffers();
}

void GrassRenderer::Shutdown()
{
    m_CullCS.reset();
    m_TrampleCS.reset();
    m_StampCS.reset();
    m_VS.reset();
    m_PS.reset();
    m_HeightSRV.Reset();
    m_GroundSRV.Reset();
    m_BladeBuffer.Reset();
    m_BladeUAV.Reset();
    m_BladeSRV.Reset();
    m_Args.Reset();
    for (int i = 0; i < 2; ++i)
    {
        m_CountStaging[i].Reset();
        m_TrampleTex[i].Reset();
        m_TrampleSRV[i].Reset();
        m_TrampleUAV[i].Reset();
    }
    m_Device = nullptr;
    m_Context = nullptr;
}

bool GrassRenderer::CreateBuffers()
{
    // ---- 葉（append）----
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(GrassBlade) * kMaxBlades;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(GrassBlade);
    if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &m_BladeBuffer))) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = kMaxBlades;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    if (FAILED(m_Device->CreateUnorderedAccessView(m_BladeBuffer.Get(), &ud, &m_BladeUAV))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = kMaxBlades;
    if (FAILED(m_Device->CreateShaderResourceView(m_BladeBuffer.Get(), &sd, &m_BladeSRV))) return false;

    // ---- 間接引数: VertexCountPerInstance, InstanceCount, StartVertex, StartInstance ----
    {
        D3D11_BUFFER_DESC ad = {};
        ad.ByteWidth = sizeof(uint32_t) * 4;
        ad.Usage = D3D11_USAGE_DEFAULT;
        ad.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ad.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        const uint32_t init[4] = { 9, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA init0 = {};
        init0.pSysMem = init;
        if (FAILED(m_Device->CreateBuffer(&ad, &init0, &m_Args))) return false;
    }
    // ---- 本数の読み戻し（ImGui 用。1 フレーム前の分を待たずに読む）----
    for (int i = 0; i < 2; ++i)
    {
        D3D11_BUFFER_DESC cd = {};
        cd.ByteWidth = 16;
        cd.Usage = D3D11_USAGE_STAGING;
        cd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateBuffer(&cd, nullptr, &m_CountStaging[i]))) return false;
    }

    // ---- 踏み跡（フィールド全体。xy = 押す向き x 強さ）----
    for (int i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = kTrampleSize;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_TrampleTex[i]))) return false;
        if (FAILED(m_Device->CreateShaderResourceView(m_TrampleTex[i].Get(), nullptr, &m_TrampleSRV[i]))) return false;
        if (FAILED(m_Device->CreateUnorderedAccessView(m_TrampleTex[i].Get(), nullptr, &m_TrampleUAV[i]))) return false;
        const float zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewFloat(m_TrampleUAV[i].Get(), zero);
    }
    return true;
}

// ============================================================
// 地形のテクスチャ（高さ場と同じ 0.5m 格子。texel の中心 = 高さ格子の中心なので、
// 線形補間で引けば GridWorld::SampleHeight と同じ値になる）
// ============================================================
void GrassRenderer::Build(const GridWorld& grid, const std::vector<uint8_t>& grassMask, uint32_t seed, TerrainGenerator::Biome biome)
{
    if (!m_Device) return;
    m_HeightSRV.Reset();
    m_GroundSRV.Reset();

    const int hw = grid.HeightW(), hd = grid.HeightD();
    const int gw = grid.Width();
    const int sub = GridWorld::kHeightSub;
    m_MapOrigin = Vector2(grid.OriginX(), grid.OriginZ());
    m_MapSize = Vector2(grid.WorldWidth(), grid.WorldDepth());

    std::vector<Vector4> ground((size_t)hw * hd);
    for (int hz = 0; hz < hd; ++hz)
        for (int hx = 0; hx < hw; ++hx)
        {
            const Vector3 p = grid.HeightCellToWorld(hx, hz);
            Vector4 c = TerrainGenerator::GroundColor(p.x, p.z, seed, biome);
            const size_t cell = (size_t)(hz / sub) * gw + (hx / sub);
            c.w = (grassMask.empty() || (cell < grassMask.size() && grassMask[cell])) ? 1.0f : 0.0f;
            ground[(size_t)hz * hw + hx] = c;
        }

    auto makeTex = [&](DXGI_FORMAT fmt, const void* data, UINT pitch, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = (UINT)hw;
            td.Height = (UINT)hd;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = fmt;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd = {};
            sd.pSysMem = data;
            sd.SysMemPitch = pitch;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
            if (FAILED(m_Device->CreateTexture2D(&td, &sd, &tex))) return false;
            return SUCCEEDED(m_Device->CreateShaderResourceView(tex.Get(), nullptr, &srv));
        };
    if (!makeTex(DXGI_FORMAT_R32_FLOAT, grid.Heights().data(), sizeof(float) * hw, m_HeightSRV)
        || !makeTex(DXGI_FORMAT_R32G32B32A32_FLOAT, ground.data(), sizeof(Vector4) * hw, m_GroundSRV))
    {
        std::cout << "[Error] GrassRenderer: terrain texture create failed" << std::endl;
        m_HeightSRV.Reset();
        m_GroundSRV.Reset();
        return;
    }

    // 古い地形の踏み跡を消す
    const float zero[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 2; ++i)
        if (m_TrampleUAV[i]) m_Context->ClearUnorderedAccessViewFloat(m_TrampleUAV[i].Get(), zero);
}

void GrassRenderer::Update(float dt, const Vector3& player, bool grounded, bool sliding)
{
    m_Time += dt;
    m_PendingDt += dt;
    m_Player = player;
    m_TrampleNow = grounded ? (sliding ? m_Settings.trampleSlideRadius : m_Settings.trampleRadius) : 0.0f;
}

void GrassRenderer::Render(Renderer& renderer, CameraBase& camera)
{
    ID3D11DeviceContext* ctx = m_Context;
    if (!m_Settings.enabled || !ctx || !m_CullCS || !m_TrampleCS || !m_VS || !m_PS
        || !m_HeightSRV || !m_GroundSRV)
        return;

    auto& s = m_Settings;
    const Matrix viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
    const Vector3 cam = camera.GetPosition();
    const float spacing = (std::max)(s.spacing, 0.05f);
    const float maxDist = (std::max)(s.maxDistance, 1.0f);

    GrassCB cb = {};
    cb.viewProj = viewProj;
    ExtractFrustumPlanes(viewProj, cb.frustum);
    cb.camPos = cam;
    cb.time = m_Time;
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.cellMin[0] = (int32_t)std::floor((cam.x - maxDist) / spacing);
    cb.cellMin[1] = (int32_t)std::floor((cam.z - maxDist) / spacing);
    const uint32_t count = (uint32_t)std::ceil(2.0f * maxDist / spacing) + 1;
    cb.cellCount[0] = cb.cellCount[1] = count;
    cb.spacing = spacing;
    cb.maxDist = maxDist;
    cb.fullDist = (std::min)(s.fullDensityDistance, maxDist);
    cb.farDensity = std::clamp(s.farDensity, 0.0f, 1.0f);
    cb.heightMin = s.heightMin;
    cb.heightMax = (std::max)(s.heightMax, s.heightMin);
    cb.widthMin = s.widthMin;
    cb.widthMax = (std::max)(s.widthMax, s.widthMin);
    cb.curlMax = s.curlMax;
    cb.maxSlope = s.maxSlope;
    // 風は世界の風（向き・波・天候の強さ）。葉の曲がり方だけがここの設定
    cb.windStrength = s.windStrength * m_Wind.strength;
    cb.windSpeed = m_Wind.waveSpeed;
    cb.windDir = m_Wind.dir;
    cb.windScale = m_Wind.waveScale;
    cb.trampleBend = s.trampleBend;
    cb.rootColor = s.rootColor;
    cb.trampleRadius = m_TrampleNow;
    cb.tipColor = s.tipColor;
    cb.trampleDecay = std::exp(-m_PendingDt / (std::max)(s.trampleRecover, 0.05f));
    cb.player = m_Player;
    cb.maxBlades = kMaxBlades;
    cb.fireIgnite = m_FireIgnite;
    cb.fireShrink = std::clamp(s.fireShrink, 0.0f, 0.95f);
    cb.fireGlow = s.fireGlow;
    cb.fireOn = m_FireSRV ? 1.0f : 0.0f;
    cb.gustPos = m_GustPos;
    cb.gustRadius = m_GustRadius;
    cb.gustDir = m_GustDir;
    cb.gustLength = m_GustLength;
    cb.gustPad = 0.0f;
    // 群れからの押し型（池が無ければ 0 = しない）。一時停止中（dt 0）は雑魚も止まっているので毎フレーム押しても減衰しない
    const bool haveAreas = m_AreaSRV && m_AreaStateSRV && m_AreaEndSRV;
    const bool haveEnemies = m_EnemySRV && m_EnemyStateSRV && m_EnemyExtraSRV && m_StampCS;
    cb.trampleExtra = Vector4(haveAreas ? s.blastTrample : 0.0f, haveAreas ? s.beamTrample : 0.0f,
        haveEnemies ? s.enemyTrample : 0.0f, (std::max)(s.enemyRadius, 0.05f));
    m_GustRadius = 0.0f;   // 1 回押したら終わり
    m_PendingDt = 0.0f;   // 一時停止中（Update 無し）は 0 のまま = 踏み跡も止まる

    // ---- 1) 踏み跡: 前のテクスチャを読んで、もう片方へ書く（爆発・光線の範囲もここで押す）----
    m_TrampleCS->WriteBuffer(ctx, 0, &cb);
    m_TrampleCS->Bind(ctx);
    m_TrampleCS->SetSRV(ctx, "g_TrampleIn", m_TrampleSRV[m_TrampleCur].Get());
    if (haveAreas)
    {
        m_TrampleCS->SetSRV(ctx, "g_Areas", m_AreaSRV);
        m_TrampleCS->SetSRV(ctx, "g_AreaStates", m_AreaStateSRV);
        m_TrampleCS->SetSRV(ctx, "g_AreaEnds", m_AreaEndSRV);
    }
    m_TrampleCS->SetUAV(ctx, "g_TrampleOut", m_TrampleUAV[1 - m_TrampleCur].Get());
    m_TrampleCS->BindUAVs(ctx);
    ctx->Dispatch((kTrampleSize + 7) / 8, (kTrampleSize + 7) / 8, 1);
    m_TrampleCS->UnbindSRVs(ctx);
    m_TrampleCS->UnbindUAVs(ctx);
    // 1b) 雑魚の足元（同じ出力へ、1 体 1 スレッドで書き込む）
    if (haveEnemies && cb.trampleExtra.z > 0.0f)
    {
        m_StampCS->WriteBuffer(ctx, 0, &cb);
        m_StampCS->Bind(ctx);
        m_StampCS->SetSRV(ctx, "g_Enemies", m_EnemySRV);
        m_StampCS->SetSRV(ctx, "g_EnemyStates", m_EnemyStateSRV);
        m_StampCS->SetSRV(ctx, "g_EnemyExtra", m_EnemyExtraSRV);
        m_StampCS->SetUAV(ctx, "g_TrampleOut", m_TrampleUAV[1 - m_TrampleCur].Get());
        m_StampCS->BindUAVs(ctx);
        ctx->Dispatch((kMaxEnemyThreads + 255) / 256, 1, 1);
        m_StampCS->UnbindSRVs(ctx);
        m_StampCS->UnbindUAVs(ctx);
    }
    m_TrampleCur = 1 - m_TrampleCur;

    // ---- 2) 生やして間引く ----
    ID3D11SamplerState* linearClamp = RenderStates::Get().LinearClamp();
    m_CullCS->WriteBuffer(ctx, 0, &cb);
    m_CullCS->Bind(ctx);
    m_CullCS->SetSRV(ctx, "g_Height", m_HeightSRV.Get());
    m_CullCS->SetSRV(ctx, "g_Ground", m_GroundSRV.Get());
    ctx->CSSetSamplers(0, 1, &linearClamp);
    m_CullCS->SetUAV(ctx, "g_OutBlades", m_BladeUAV.Get(), 0);   // 数を 0 から
    m_CullCS->BindUAVs(ctx);
    ctx->Dispatch((count + 15) / 16, (count + 15) / 16, 1);
    m_CullCS->UnbindSRVs(ctx);
    m_CullCS->UnbindUAVs(ctx);
    ctx->CopyStructureCount(m_Args.Get(), sizeof(uint32_t), m_BladeUAV.Get());

    // 本数（ImGui 用）: 今フレームの分を写し、前のフレームの分を待たずに読む
    ctx->CopyStructureCount(m_CountStaging[m_Frame & 1].Get(), 0, m_BladeUAV.Get());
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (m_Frame > 0 && SUCCEEDED(ctx->Map(m_CountStaging[(m_Frame + 1) & 1].Get(), 0, D3D11_MAP_READ,
            D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)))
        {
            m_LastCount = *static_cast<const uint32_t*>(mapped.pData);
            ctx->Unmap(m_CountStaging[(m_Frame + 1) & 1].Get(), 0);
        }
    }
    ++m_Frame;

    // ---- 3) 描く（頂点バッファ無し。葉 1 本 = 9 頂点）----
    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = cam;
    m_VS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 1, &light);
    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    m_VS->SetSRV(ctx, "g_Blades", m_BladeSRV.Get());
    m_VS->SetSRV(ctx, "g_Trample", m_TrampleSRV[m_TrampleCur].Get());
    m_VS->SetSRV(ctx, "g_Ground", m_GroundSRV.Get());
    if (m_FireSRV) m_VS->SetSRV(ctx, "g_Fire", m_FireSRV);
    if (m_WindSourceSRV) m_VS->SetSRV(ctx, "g_WindSources", m_WindSourceSRV);
    ctx->VSSetSamplers(0, 1, &linearClamp);
    PointLightManager::Get().BindPS(ctx);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(rs.DepthDefault(), 0);
    ctx->RSSetState(rs.CullNone());   // 葉は両面

    ctx->DrawInstancedIndirect(m_Args.Get(), 0);

    // 次のフレームの CS が UAV にするので外す
    m_VS->UnbindSRVs(ctx);
    PointLightManager::Get().UnbindPS(ctx);
    rs.Restore(ctx);
}

// TEMP-TEST: 踏み跡の読み戻し（自動テスト envfx）
bool GrassRenderer::DebugTrampleStats(const Vector2& centerXZ, float radius, float minStrength, int& count, float& maxStrength)
{
    count = 0;
    maxStrength = 0.0f;
    if (!m_Context || !m_Device || !m_TrampleTex[m_TrampleCur]) return false;
    if (!m_TrampleStaging)
    {
        D3D11_TEXTURE2D_DESC td = {};
        m_TrampleTex[0]->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_TrampleStaging))) return false;
    }
    m_Context->CopyResource(m_TrampleStaging.Get(), m_TrampleTex[m_TrampleCur].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_TrampleStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const float texel = m_MapSize.x / (float)kTrampleSize;
    const int cx = (int)((centerXZ.x - m_MapOrigin.x) / texel), cy = (int)((centerXZ.y - m_MapOrigin.y) / texel);
    const int r = (int)std::ceil(radius / texel);
    for (int y = (std::max)(cy - r, 0); y <= (std::min)(cy + r, kTrampleSize - 1); ++y)
    {
        const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(
            static_cast<const uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch);
        for (int x = (std::max)(cx - r, 0); x <= (std::min)(cx + r, kTrampleSize - 1); ++x)
        {
            const float wx = m_MapOrigin.x + ((float)x + 0.5f) * texel, wz = m_MapOrigin.y + ((float)y + 0.5f) * texel;
            if ((wx - centerXZ.x) * (wx - centerXZ.x) + (wz - centerXZ.y) * (wz - centerXZ.y) > radius * radius) continue;
            const float vx = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 2]);
            const float vy = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 2 + 1]);
            const float s = std::sqrt(vx * vx + vy * vy);
            if (s >= minStrength) ++count;
            maxStrength = (std::max)(maxStrength, s);
        }
    }
    m_Context->Unmap(m_TrampleStaging.Get(), 0);
    return true;
}

void GrassRenderer::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Grass")) return;

    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##grass", &s.enabled);
    ImGui::SameLine();
    ImGui::Text("blades %u / %u", m_LastCount, kMaxBlades);
    ImGui::DragFloat("Spacing", &s.spacing, 0.005f, 0.05f, 1.0f, "%.3f m");
    ImGui::DragFloat("Max Distance##grass", &s.maxDistance, 0.5f, 5.0f, 150.0f, "%.0f m");
    ImGui::DragFloat("Full Density To", &s.fullDensityDistance, 0.5f, 0.0f, 150.0f, "%.0f m");
    ImGui::SliderFloat("Far Density", &s.farDensity, 0.0f, 1.0f);
    ImGui::DragFloatRange2("Height##grass", &s.heightMin, &s.heightMax, 0.005f, 0.02f, 2.0f, "%.2f m");
    ImGui::DragFloatRange2("Width##grass", &s.widthMin, &s.widthMax, 0.001f, 0.005f, 0.3f, "%.3f m");
    ImGui::SliderFloat("Curl", &s.curlMax, 0.0f, 0.9f);
    ImGui::DragFloat("Max Slope##grass", &s.maxSlope, 0.02f, 0.1f, 5.0f);
    ImGui::ColorEdit3("Root Color", &s.rootColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit3("Tip Color", &s.tipColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SeparatorText("Wind");
    ImGui::SliderFloat("Bend at wind x1", &s.windStrength, 0.0f, 1.0f);
    ImGui::TextDisabled("direction / speed / waves: Weather panel (World Wind), strength now x%.2f", m_Wind.strength);
    ImGui::SeparatorText("Fire (GrassFireField)");
    ImGui::SliderFloat("Burnt Shrink", &s.fireShrink, 0.0f, 0.95f);
    ImGui::DragFloat("Ember Glow", &s.fireGlow, 0.05f, 0.0f, 10.0f);
    ImGui::SeparatorText("Trample");
    ImGui::DragFloat("Radius##trample", &s.trampleRadius, 0.01f, 0.0f, 3.0f, "%.2f m");
    ImGui::DragFloat("Slide Radius", &s.trampleSlideRadius, 0.01f, 0.0f, 3.0f, "%.2f m");
    ImGui::DragFloat("Recover", &s.trampleRecover, 0.05f, 0.05f, 20.0f, "%.2f s");
    ImGui::SliderFloat("Bend##trample", &s.trampleBend, 0.0f, 1.0f);
    ImGui::SeparatorText("Trample from the swarm");
    ImGui::DragFloat("Blast Radius x", &s.blastTrample, 0.02f, 0.0f, 3.0f);
    ImGui::DragFloat("Beam Band x radius", &s.beamTrample, 0.05f, 0.0f, 5.0f);
    ImGui::SliderFloat("Enemy Feet Strength", &s.enemyTrample, 0.0f, 1.0f);
    ImGui::DragFloat("Enemy Feet Radius", &s.enemyRadius, 0.01f, 0.05f, 1.5f, "%.2f m");
    ImGui::TextDisabled("areas %s  enemies %s", m_AreaSRV ? "on" : "off", m_EnemySRV ? "on" : "off");
}
