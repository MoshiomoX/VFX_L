// ============================================================
// SandField.cpp
// ============================================================
#include "World/SandField.h"
#include "World/GridWorld.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Swarm/AreaProfile.h"
#include "Swarm/SwarmVFXTable.h"
#include "VFX_Editor/VFXId.h"
#include "imgui.h"

#include <Windows.h>

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

static_assert(sizeof(SandField::SandCB) == 144, "SandCB layout mismatch");
static_assert(sizeof(SandField::TrackCB) == 32, "SandTrackCB layout mismatch");

namespace
{
    std::shared_ptr<ComputeShader> LoadCS(ID3D11Device* device, const wchar_t* path)
    {
        auto cs = std::make_shared<ComputeShader>();
        if (FAILED(ShaderPath::Load(cs.get(), device, path))) return nullptr;
        return cs;
    }

    bool MakeTex(ID3D11Device* device, DXGI_FORMAT fmt, Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv, Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& uav)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = SandField::kSize;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        return SUCCEEDED(device->CreateTexture2D(&td, nullptr, &tex))
            && SUCCEEDED(device->CreateShaderResourceView(tex.Get(), nullptr, &srv))
            && SUCCEEDED(device->CreateUnorderedAccessView(tex.Get(), nullptr, &uav));
    }
}

bool SandField::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    // TEMP-TEST: VFXL_SAND_FIELD=0 で切る（比較用）
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_SAND_FIELD", env, sizeof(env)) > 0)
        m_Settings.enabled = (env[0] != '0');

    m_StepCS = LoadCS(device, L"Shader/Sand/SandFieldCS.hlsl");
    m_TrackCS = LoadCS(device, L"Shader/Sand/SandTrackCS.hlsl");
    if (!m_StepCS || !m_TrackCS)
    {
        std::cout << "[Error] SandField: shader load failed" << std::endl;
        return false;
    }
    // 状態は 32 bit：乾きの戻りは 1 フレームに 0.001 ほど、16 bit だと乾ききる手前で丸めで止まる（草の延焼と同じ理由）
    for (int i = 0; i < 2; ++i)
    {
        if (!MakeTex(device, DXGI_FORMAT_R32G32B32A32_FLOAT, m_Tex[i], m_SRV[i], m_UAV[i])) return false;
        if (!MakeTex(device, DXGI_FORMAT_R32_FLOAT, m_TrackTex[i], m_TrackSRV[i], m_TrackUAV[i])) return false;
    }
    // 1×1 の白（GrassFireEmitCS は「草が生える所」= a を見るので、砂漠ではどこでも出せるように）
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = 1;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const uint32_t white = 0xFFFFFFFFu;
        D3D11_SUBRESOURCE_DATA sd = { &white, 4, 4 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
        if (SUCCEEDED(device->CreateTexture2D(&td, &sd, &t)))
            device->CreateShaderResourceView(t.Get(), nullptr, &m_WhiteSRV);
    }
    Reset();
    return true;
}

void SandField::Shutdown()
{
    m_StepCS.reset();
    m_TrackCS.reset();
    for (int i = 0; i < 2; ++i)
    {
        m_Tex[i].Reset(); m_SRV[i].Reset(); m_UAV[i].Reset();
        m_TrackTex[i].Reset(); m_TrackSRV[i].Reset(); m_TrackUAV[i].Reset();
    }
    m_TableBuffer.Reset();
    m_TableSRV.Reset();
    m_WhiteSRV.Reset();
    m_WaterSRV.Reset();
    m_Staging.Reset();
    m_TrackStaging.Reset();
    m_Ready = false;
    m_Device = nullptr;
    m_Context = nullptr;
}

void SandField::Build(const GridWorld& grid)
{
    m_MapOrigin = Vector2(grid.OriginX(), grid.OriginZ());
    m_MapSize = Vector2(grid.WorldWidth(), grid.WorldDepth());
    // 池のマス（512² へ。テクセルの中心が水のマスなら 1）。池の上では砂が乾かず・舞わない
    m_WaterSRV.Reset();
    if (m_Device)
    {
        std::vector<uint8_t> water((size_t)kSize * kSize, 0);
        const float cs = GridWorld::kCellSize;
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x)
            {
                const float wx = m_MapSize.x * (x + 0.5f) / kSize;
                const float wz = m_MapSize.y * (y + 0.5f) / kSize;
                const int gx = (int)(wx / cs), gz = (int)(wz / cs);
                if (gx >= 0 && gz >= 0 && gx < grid.Width() && gz < grid.Depth() && grid.IsWater(gx, gz))
                    water[(size_t)y * kSize + x] = 255;
            }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = kSize;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = { water.data(), (UINT)kSize, 0 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
        if (SUCCEEDED(m_Device->CreateTexture2D(&td, &sd, &t)))
            m_Device->CreateShaderResourceView(t.Get(), nullptr, &m_WaterSRV);
    }
    m_Ready = (m_StepCS != nullptr && m_TrackCS != nullptr);
    Reset();
}

// 全部を「冷たい・乾いた・平らな」砂に
void SandField::Reset()
{
    if (!m_Context) return;
    const float state[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 2; ++i)
    {
        if (m_UAV[i]) m_Context->ClearUnorderedAccessViewFloat(m_UAV[i].Get(), state);
        if (m_TrackUAV[i]) m_Context->ClearUnorderedAccessViewFloat(m_TrackUAV[i].Get(), zero);
    }
    m_PendingDt = 0.0f;
    m_StampOn = false;
}

// 範囲の profile の heat / wet → VFX レシピ毎（Area::vfxType と同じ添字）。同じレシピの範囲が複数なら大きい方
void SandField::BuildTable(const SwarmVFXTable& table)
{
    m_TableBuffer.Reset();
    m_TableSRV.Reset();
    m_HazeVfx = table.IndexOf(VFXId::SandHaze);
    m_DustVfx = table.IndexOf(VFXId::SandDust);

    std::vector<Vector2> rows((size_t)(std::max)(table.GetRecipeCount(), 1), Vector2::Zero);
    for (int i = 1; i < AreaProfileDB::Count(); ++i)
    {
        const AreaProfile& p = AreaProfileDB::At(i);
        if (p.heat == 0.0f && p.wet <= 0.0f) continue;
        const uint32_t idx = table.IndexOf(AreaProfileDB::FindVFXId(p.vfxFile));
        if (idx == 0 || idx >= rows.size()) continue;   // CPU で見た目を出す範囲は GPU の表に無い
        if (std::abs(p.heat) > std::abs(rows[idx].x)) rows[idx].x = p.heat;
        rows[idx].y = (std::max)(rows[idx].y, p.wet);
    }
    if (!m_Device) return;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(Vector2) * rows.size());
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Vector2);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = rows.data();
    if (FAILED(m_Device->CreateBuffer(&bd, &sd, &m_TableBuffer))) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = DXGI_FORMAT_UNKNOWN;
    vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    vd.Buffer.NumElements = (UINT)rows.size();
    if (FAILED(m_Device->CreateShaderResourceView(m_TableBuffer.Get(), &vd, &m_TableSRV))) m_TableBuffer.Reset();

    int hot = 0, wet = 0;
    for (const Vector2& r : rows) { hot += (r.x != 0.0f) ? 1 : 0; wet += (r.y > 0.0f) ? 1 : 0; }
    std::cout << "[SandField] table: " << hot << " hot/cold, " << wet << " wet recipe(s) of " << rows.size()
        << ", SandHaze recipe " << m_HazeVfx << ", SandDust recipe " << m_DustVfx << std::endl;
}

void SandField::SetWind(const WindState& wind, float time)
{
    const Vector3 v = wind.Velocity(time, m_Player.x, m_Player.z);
    m_Wind = Vector2(v.x, v.z);
}

void SandField::Stamp(const Vector3& pos, float radius, float heat, float wet)
{
    m_Stamp = Vector4(pos.x, pos.z, radius, heat);
    m_StampWet = wet;
    m_StampOn = true;
}

void SandField::Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates,
    ID3D11ShaderResourceView* enemies, ID3D11ShaderResourceView* enemyStates, ID3D11ShaderResourceView* enemyExtra,
    ID3D11ShaderResourceView* height, float groundY, ID3D11ShaderResourceView* windSources)
{
    if (!Active() || !m_Context || !m_TableSRV || !areas || !areaStates || !windSources || !m_WaterSRV) return;

    const auto& s = m_Settings;
    SandCB cb = {};
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.wind = m_Wind;
    cb.dt = (std::min)(m_PendingDt, 0.1f);   // 一時停止中（Tick 無し）は 0 = 止まる
    cb.texel = m_MapSize.x / (float)kSize;
    const float stable = 0.24f / (std::max)(cb.dt, 1e-3f);   // 陽解法の安定の限界
    cb.heatDiffuse = (std::min)(s.heatDiffuse, stable);
    cb.cool = s.cool;
    cb.dryRate = s.dryRate;
    cb.heatDry = s.heatDry;
    cb.hazeHeat = s.hazeHeat;
    cb.trackFill = s.trackFill;
    cb.trackBlur = (std::min)(s.trackBlur, stable);
    cb.trackWind = s.trackWind;
    cb.player = m_Player;
    cb.playerOn = (m_PlayerGrounded && cb.dt > 0.0f) ? 1.0f : 0.0f;
    cb.playerRadius = s.playerRadius;
    cb.playerDepth = -s.playerDepth;
    cb.craterDepth = s.craterDepth;
    cb.sourceMul = s.sourceMul;
    cb.stamp = m_StampOn ? m_Stamp : Vector4::Zero;
    cb.stampWet = m_StampOn ? m_StampWet : 0.0f;
    cb.liftWind = s.dustLiftWind;
    cb.liftRange = s.dustRange;
    cb.worldLift = s.dustWorldShare;
    cb.trackWindFill = s.trackWindFill;
    m_PendingDt = 0.0f;
    m_StampOn = false;

    const int next = 1 - m_Cur;
    m_StepCS->WriteBuffer(m_Context, 0, &cb);
    m_StepCS->Bind(m_Context);
    m_StepCS->SetSRV(m_Context, "g_In", m_SRV[m_Cur].Get());
    m_StepCS->SetSRV(m_Context, "g_InTrack", m_TrackSRV[m_Cur].Get());
    m_StepCS->SetSRV(m_Context, "g_Areas", areas);
    m_StepCS->SetSRV(m_Context, "g_AreaStates", areaStates);
    m_StepCS->SetSRV(m_Context, "g_RecipeSand", m_TableSRV.Get());
    m_StepCS->SetSRV(m_Context, "g_WindSources", windSources);
    m_StepCS->SetSRV(m_Context, "g_Water", m_WaterSRV.Get());
    m_StepCS->SetUAV(m_Context, "g_Out", m_UAV[next].Get());
    m_StepCS->SetUAV(m_Context, "g_OutTrack", m_TrackUAV[next].Get());
    m_StepCS->BindUAVs(m_Context);
    m_Context->Dispatch((kSize + 7) / 8, (kSize + 7) / 8, 1);
    m_StepCS->UnbindSRVs(m_Context);
    m_StepCS->UnbindUAVs(m_Context);

    // 雑魚の足跡（同じ跡の図へ min）。止まっている間は付けない
    if (enemies && enemyStates && enemyExtra && height && cb.dt > 0.0f)
    {
        TrackCB tcb = { m_MapOrigin, m_MapSize, s.mobRadius, -s.mobDepth, groundY, 1.0f };
        m_TrackCS->WriteBuffer(m_Context, 0, &tcb);
        m_TrackCS->Bind(m_Context);
        m_TrackCS->SetSRV(m_Context, "g_Enemies", enemies);
        m_TrackCS->SetSRV(m_Context, "g_EnemyStates", enemyStates);
        m_TrackCS->SetSRV(m_Context, "g_EnemyExtra", enemyExtra);
        m_TrackCS->SetSRV(m_Context, "g_Height", height);
        m_TrackCS->SetUAV(m_Context, "g_Track", m_TrackUAV[next].Get());
        m_TrackCS->BindUAVs(m_Context);
        m_Context->Dispatch((4096 + 255) / 256, 1, 1);   // = Swarm::kMaxEnemies（範囲外は CS が数で弾く）
        m_TrackCS->UnbindSRVs(m_Context);
        m_TrackCS->UnbindUAVs(m_Context);
    }
    m_Cur = next;
}

ID3D11ShaderResourceView* SandField::GetStateSRV() const
{
    return Active() ? m_SRV[m_Cur].Get() : nullptr;
}

ID3D11ShaderResourceView* SandField::GetTrackSRV() const
{
    return Active() ? m_TrackSRV[m_Cur].Get() : nullptr;
}

bool SandField::FillEmitInput(GrassFireEmitInput& out, const Vector3& camPos, ID3D11ShaderResourceView* height) const
{
    if (!Active() || m_HazeVfx == 0 || !m_WhiteSRV || !height || m_Settings.hazeRate <= 0.0f) return false;
    out.fire = m_SRV[m_Cur].Get();   // x = 温度、y = 乾き、z = 陽炎の印 → 強さ = (温度 - hazeHeat) × 乾き
    out.ground = m_WhiteSRV.Get();
    out.height = height;
    out.mapOrigin = m_MapOrigin;
    out.mapSize = m_MapSize;
    out.camPos = camPos;
    out.emitRadius = m_Settings.emitRadius;
    out.ignite = m_Settings.hazeHeat;
    out.rateMul = m_Settings.hazeRate;
    out.vfx = m_HazeVfx;
    out.heightOffset = 0.1f;
    out.fireSize = kSize;
    return true;
}

bool SandField::FillDustInput(GrassFireEmitInput& out, const Vector3& camPos, ID3D11ShaderResourceView* height) const
{
    if (!Active() || m_DustVfx == 0 || !m_WhiteSRV || !height || m_Settings.dustRate <= 0.0f) return false;
    out.fire = m_SRV[m_Cur].Get();   // w = 砂が舞う強さ（SandFieldCS が乾き × 風で書く）
    out.ground = m_WhiteSRV.Get();
    out.height = height;
    out.mapOrigin = m_MapOrigin;
    out.mapSize = m_MapSize;
    out.camPos = camPos;
    out.emitRadius = m_Settings.dustRadius;
    out.ignite = 0.0f;
    out.rateMul = m_Settings.dustRate;
    out.vfx = m_DustVfx;
    out.heightOffset = 0.03f;
    out.fireSize = kSize;
    out.channel = 1;
    return true;
}

// TEMP-TEST: 自動テストの検証用。Map で GPU を待つので毎フレーム呼ばない
bool SandField::DebugReadAt(const Vector3& pos, float& temp, float& dry, float& depth, float* lift) const
{
    temp = dry = depth = 0.0f;
    if (lift) *lift = 0.0f;
    if (!m_Context || !m_Device || !m_Tex[m_Cur]) return false;
    auto makeStaging = [&](ID3D11Texture2D* src, Microsoft::WRL::ComPtr<ID3D11Texture2D>& out) -> bool
        {
            if (out) return true;
            D3D11_TEXTURE2D_DESC td = {};
            src->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING;
            td.BindFlags = 0;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            return SUCCEEDED(m_Device->CreateTexture2D(&td, nullptr, &out));
        };
    if (!makeStaging(m_Tex[0].Get(), m_Staging) || !makeStaging(m_TrackTex[0].Get(), m_TrackStaging)) return false;
    const int x = std::clamp((int)((pos.x - m_MapOrigin.x) / m_MapSize.x * kSize), 0, kSize - 1);
    const int y = std::clamp((int)((pos.z - m_MapOrigin.y) / m_MapSize.y * kSize), 0, kSize - 1);
    m_Context->CopyResource(m_Staging.Get(), m_Tex[m_Cur].Get());
    m_Context->CopyResource(m_TrackStaging.Get(), m_TrackTex[m_Cur].Get());
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    const float* px = reinterpret_cast<const float*>(static_cast<const uint8_t*>(m.pData) + (size_t)y * m.RowPitch) + x * 4;
    temp = px[0];
    dry = px[1];
    if (lift) *lift = px[3];
    m_Context->Unmap(m_Staging.Get(), 0);
    if (FAILED(m_Context->Map(m_TrackStaging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    depth = reinterpret_cast<const float*>(static_cast<const uint8_t*>(m.pData) + (size_t)y * m.RowPitch)[x];
    m_Context->Unmap(m_TrackStaging.Get(), 0);
    return true;
}

void SandField::DrawImGui(const Vector3* player)
{
    if (!ImGui::CollapsingHeader("Sand Field (desert)")) return;
    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##sand", &s.enabled);
    ImGui::SameLine();
    ImGui::TextDisabled(m_Active ? (m_Ready ? "active" : "not built") : "not a desert stage");
    if (player)
    {
        if (ImGui::Button("Heat at player (r 3m)")) Stamp(*player, 3.0f, 1.2f, 0.0f);
        ImGui::SameLine();
        if (ImGui::Button("Wet at player (r 3m)")) Stamp(*player, 3.0f, 0.0f, 1.0f);
    }
    if (ImGui::Button("Reset sand")) Reset();
    ImGui::DragFloat("Heat Spread", &s.heatDiffuse, 0.02f, 0.0f, 10.0f);
    ImGui::DragFloat("Cooling (1/s)", &s.cool, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Drying (1/s)", &s.dryRate, 0.002f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Drying x heat", &s.heatDry, 0.05f, 0.0f, 20.0f);
    ImGui::SliderFloat("Haze above temp", &s.hazeHeat, 0.05f, 1.5f);
    ImGui::DragFloat("Haze x", &s.hazeRate, 0.02f, 0.0f, 5.0f);
    ImGui::DragFloat("Haze Radius (m)", &s.emitRadius, 0.5f, 5.0f, 200.0f);
    ImGui::SeparatorText("Blowing sand (wind)");
    ImGui::DragFloat("Lift Wind (m/s)", &s.dustLiftWind, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Full Above (m/s)", &s.dustRange, 0.05f, 0.1f, 20.0f);
    ImGui::SliderFloat("World Wind Share", &s.dustWorldShare, 0.0f, 1.0f);
    ImGui::DragFloat("Dust x", &s.dustRate, 0.02f, 0.0f, 5.0f);
    ImGui::DragFloat("Dust Radius (m)", &s.dustRadius, 0.5f, 5.0f, 200.0f);
    ImGui::DragFloat("Dents fill x (blowing)", &s.trackWindFill, 0.1f, 0.0f, 50.0f);
    ImGui::Separator();
    ImGui::DragFloat("Track Fill (1/s)", &s.trackFill, 0.002f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Track Blur", &s.trackBlur, 0.02f, 0.0f, 5.0f);
    ImGui::DragFloat("Track x wind", &s.trackWind, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Player Print r / depth", &s.playerRadius, 0.01f, 0.05f, 2.0f);
    ImGui::DragFloat("Player Print depth (m)", &s.playerDepth, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Mob Print r (m)", &s.mobRadius, 0.01f, 0.05f, 2.0f);
    ImGui::DragFloat("Mob Print depth (m)", &s.mobDepth, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Crater depth (m)", &s.craterDepth, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Source Radius x", &s.sourceMul, 0.02f, 0.1f, 3.0f);
    ImGui::SliderFloat("Wet sand brightness", &s.wetDark, 0.2f, 1.0f);
    ImGui::SliderFloat("Hot sand tint", &s.heatTint, 0.0f, 1.0f);
    ImGui::SliderFloat("Track normal", &s.trackStrength, 0.0f, 4.0f);
    ImGui::Text("wind %.2f %.2f m/s, SandHaze recipe %u, SandDust recipe %u", m_Wind.x, m_Wind.y, m_HazeVfx, m_DustVfx);
    ImGui::Checkbox("Show map (red = heat, green = dry)", &m_ShowMap);
    if (m_ShowMap && m_SRV[m_Cur])
        ImGui::Image((ImTextureID)m_SRV[m_Cur].Get(), ImVec2(256, 256));
}
