// ============================================================
// GrassFireField.cpp
// ============================================================
#include "World/GrassFireField.h"
#include "World/GridWorld.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Swarm/AreaProfile.h"
#include "Swarm/SwarmVFXTable.h"
#include "imgui.h"

#include <DirectXPackedVector.h>

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

static_assert(sizeof(GrassFireField::FireCB) == 96, "GrassFireCB layout mismatch");

bool GrassFireField::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    // TEMP-TEST: 既定は切（ユーザーの判断待ち）。VFXL_GRASS_FIRE=1 で入れる
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_GRASS_FIRE", env, sizeof(env)) > 0 && env[0] == '1')
        m_Settings.enabled = true;

    m_StepCS = std::make_shared<ComputeShader>();
    if (FAILED(ShaderPath::Load(m_StepCS.get(), device, L"Shader/Grass/GrassFireCS.hlsl")))
    {
        std::cout << "[Error] GrassFireField: GrassFireCS load failed" << std::endl;
        m_StepCS.reset();
        return false;
    }

    // 温度 / 燃料（R16G16F の 2 枚を交互に読み書き。踏み跡と同じ作り）
    for (int i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = kSize;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &m_Tex[i]))) return false;
        if (FAILED(device->CreateShaderResourceView(m_Tex[i].Get(), nullptr, &m_SRV[i]))) return false;
        if (FAILED(device->CreateUnorderedAccessView(m_Tex[i].Get(), nullptr, &m_UAV[i]))) return false;
    }
    Reset();
    return true;
}

void GrassFireField::Shutdown()
{
    m_StepCS.reset();
    for (int i = 0; i < 2; ++i) { m_Tex[i].Reset(); m_SRV[i].Reset(); m_UAV[i].Reset(); }
    m_HeatBuffer.Reset();
    m_HeatSRV.Reset();
    m_Staging.Reset();
    m_Ready = false;
    m_Device = nullptr;
    m_Context = nullptr;
}

void GrassFireField::Build(const GridWorld& grid)
{
    m_MapOrigin = Vector2(grid.OriginX(), grid.OriginZ());
    m_MapSize = Vector2(grid.WorldWidth(), grid.WorldDepth());
    m_Ready = (m_StepCS != nullptr);
    Reset();
}

// 温度 0、燃料 1（全部の草が生えている）
void GrassFireField::Reset()
{
    if (!m_Context) return;
    const float fresh[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    for (int i = 0; i < 2; ++i)
        if (m_UAV[i]) m_Context->ClearUnorderedAccessViewFloat(m_UAV[i].Get(), fresh);
    m_StampHeat = 0.0f;
}

// 範囲の profile の heat → VFX レシピ毎（Area::vfxType と同じ添字）。同じレシピを使う範囲が複数なら大きい方
void GrassFireField::BuildHeatTable(const SwarmVFXTable& table)
{
    m_HeatBuffer.Reset();
    m_HeatSRV.Reset();
    m_FireVfx = table.IndexOf(VFXId::GrassFire);

    std::vector<float> heat((size_t)(std::max)(table.GetRecipeCount(), 1), 0.0f);
    for (int i = 1; i < AreaProfileDB::Count(); ++i)
    {
        const AreaProfile& p = AreaProfileDB::At(i);
        if (p.heat <= 0.0f) continue;
        const uint32_t idx = table.IndexOf(AreaProfileDB::FindVFXId(p.vfxFile));
        if (idx == 0 || idx >= heat.size()) continue;   // CPU で見た目を出す範囲は GPU の熱源にならない
        heat[idx] = (std::max)(heat[idx], p.heat);
    }

    if (!m_Device) return;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(float) * heat.size());
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(float);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = heat.data();
    if (FAILED(m_Device->CreateBuffer(&bd, &sd, &m_HeatBuffer))) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = DXGI_FORMAT_UNKNOWN;
    vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    vd.Buffer.NumElements = (UINT)heat.size();
    if (FAILED(m_Device->CreateShaderResourceView(m_HeatBuffer.Get(), &vd, &m_HeatSRV))) m_HeatBuffer.Reset();

    int hot = 0;
    for (float h : heat) hot += (h > 0.0f) ? 1 : 0;
    std::cout << "[GrassFire] heat table: " << hot << " hot recipe(s) of " << heat.size()
        << ", GrassFire recipe " << m_FireVfx << std::endl;
}

void GrassFireField::Ignite(const Vector3& pos, float radius, float heat)
{
    m_Stamp = Vector3(pos.x, pos.z, radius);
    m_StampHeat = heat;
}

void GrassFireField::Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* ground)
{
    if (!Active() || !m_Context || !m_HeatSRV || !ground) return;

    auto& s = m_Settings;
    FireCB cb = {};
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.windDir = m_Wind.dir;
    cb.windSpeed = m_Wind.speed * m_Wind.strength;
    cb.dt = (std::min)(m_PendingDt, 0.1f);   // 一時停止中（Tick 無し）は 0 = 火も止まる
    cb.ignite = s.ignite;
    cb.cool = s.cool;
    cb.diffuse = (std::min)(s.diffuse, 0.24f / (std::max)(cb.dt, 1e-3f));   // 陽解法の安定の限界
    cb.downwind = s.downwind;
    cb.burnRate = s.burnRate;
    cb.release = s.release;
    cb.regrow = s.regrow;
    cb.rain = std::clamp(m_Rain, 0.0f, 1.0f);
    cb.stamp = m_Stamp;
    cb.stampHeat = m_StampHeat;
    cb.sourceMul = s.sourceMul;
    cb.texel = m_MapSize.x / (float)kSize;
    cb.rainCool = s.rainCool;
    m_PendingDt = 0.0f;
    m_StampHeat = 0.0f;

    m_StepCS->WriteBuffer(m_Context, 0, &cb);
    m_StepCS->Bind(m_Context);
    m_StepCS->SetSRV(m_Context, "g_In", m_SRV[m_Cur].Get());
    m_StepCS->SetSRV(m_Context, "g_Ground", ground);
    m_StepCS->SetSRV(m_Context, "g_Areas", areas);
    m_StepCS->SetSRV(m_Context, "g_AreaStates", areaStates);
    m_StepCS->SetSRV(m_Context, "g_RecipeHeat", m_HeatSRV.Get());
    m_StepCS->SetUAV(m_Context, "g_Out", m_UAV[1 - m_Cur].Get());
    m_StepCS->BindUAVs(m_Context);
    m_Context->Dispatch((kSize + 7) / 8, (kSize + 7) / 8, 1);
    m_StepCS->UnbindSRVs(m_Context);
    m_StepCS->UnbindUAVs(m_Context);
    m_Cur = 1 - m_Cur;
}

ID3D11ShaderResourceView* GrassFireField::GetSRV() const
{
    return Active() ? m_SRV[m_Cur].Get() : nullptr;
}

bool GrassFireField::FillEmitInput(GrassFireEmitInput& out, const Vector3& camPos,
    ID3D11ShaderResourceView* ground, ID3D11ShaderResourceView* height) const
{
    if (!Active() || m_FireVfx == 0 || !ground || !height || m_Settings.emitRate <= 0.0f) return false;
    out.fire = m_SRV[m_Cur].Get();
    out.ground = ground;
    out.height = height;
    out.mapOrigin = m_MapOrigin;
    out.mapSize = m_MapSize;
    out.camPos = camPos;
    out.emitRadius = m_Settings.emitRadius;
    out.ignite = m_Settings.ignite;
    out.rateMul = m_Settings.emitRate;
    out.vfx = m_FireVfx;
    out.heightOffset = 0.05f;
    out.fireSize = kSize;
    return true;
}

// TEMP-TEST: 自動テストの検証用。Map で GPU を待つので毎フレーム呼ばない
bool GrassFireField::DebugReadStats(uint32_t& burning, uint32_t& charred) const
{
    burning = charred = 0;
    if (!m_Context || !m_Device || !m_Tex[m_Cur]) return false;
    if (!m_Staging)
    {
        D3D11_TEXTURE2D_DESC td = {};
        m_Tex[0]->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_Staging))) return false;
    }
    m_Context->CopyResource(m_Staging.Get(), m_Tex[m_Cur].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    for (int y = 0; y < kSize; ++y)
    {
        const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(
            static_cast<const uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch);
        for (int x = 0; x < kSize; ++x)
        {
            const float t = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 2]);
            const float fuel = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 2 + 1]);
            if (t > m_Settings.ignite && fuel > 0.001f) ++burning;
            if (fuel < 0.5f) ++charred;
        }
    }
    m_Context->Unmap(m_Staging.Get(), 0);
    return true;
}

void GrassFireField::DrawImGui(const Vector3* player)
{
    if (!ImGui::CollapsingHeader("Grass Fire")) return;
    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##fire", &s.enabled);
    ImGui::SameLine();
    ImGui::TextDisabled(m_Active ? (m_Ready ? "active" : "not built") : "no grass on this stage");
    if (player && ImGui::Button("Ignite at player (r 2m)")) Ignite(*player, 2.0f);
    ImGui::SameLine();
    if (ImGui::Button("Extinguish all")) Reset();
    ImGui::SliderFloat("Ignite Temp", &s.ignite, 0.05f, 1.5f);
    ImGui::DragFloat("Cooling (1/s)", &s.cool, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Spread (diffuse)", &s.diffuse, 0.02f, 0.0f, 10.0f);
    ImGui::DragFloat("Downwind x wind", &s.downwind, 0.02f, 0.0f, 3.0f);
    ImGui::DragFloat("Burn Rate (1/s)", &s.burnRate, 0.01f, 0.01f, 5.0f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("(%.1f s per cell)", 1.0f / (std::max)(s.burnRate, 0.01f));
    ImGui::DragFloat("Heat Release (1/s)", &s.release, 0.02f, 0.0f, 10.0f);
    ImGui::DragFloat("Regrow (1/s)", &s.regrow, 0.001f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Rain Cooling x", &s.rainCool, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Source Radius x", &s.sourceMul, 0.02f, 0.1f, 3.0f);
    ImGui::DragFloat("Particles x", &s.emitRate, 0.02f, 0.0f, 5.0f);
    ImGui::DragFloat("Particle Radius (m)", &s.emitRadius, 0.5f, 5.0f, 200.0f);
    ImGui::Text("wind %.2f m/s, rain %.2f, GrassFire recipe %u", m_Wind.speed * m_Wind.strength, m_Rain, m_FireVfx);
    ImGui::Checkbox("Show map (red = heat, green = fuel)", &m_ShowMap);
    if (m_ShowMap && m_SRV[m_Cur])
        ImGui::Image((ImTextureID)m_SRV[m_Cur].Get(), ImVec2(256, 256));
}
