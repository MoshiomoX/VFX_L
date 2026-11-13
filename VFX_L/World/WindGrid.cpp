// ============================================================
// WindGrid.cpp
// ============================================================
#include "World/WindGrid.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Debug/FrameProfiler.h"
#include "imgui.h"

#include <DirectXPackedVector.h>

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

WindGrid* WindGrid::s_Current = nullptr;

namespace
{
    // WindGridStepCS の b0 と同じ並び
    struct StepCB
    {
        Vector3 origin; float cell;
        Vector3 prevOrigin; float dt;
        float decay, relax, heightFull, heightZero;
        Vector2 advectWind; float feetY, prevValid;
        int dims[3]; float motorGain;
    };
    static_assert(sizeof(StepCB) == 80, "WindGridCB: HLSL 側と不一致");

    struct DiffuseCB
    {
        int dims[3]; float diffuse;
    };
    static_assert(sizeof(DiffuseCB) == 16, "WindDiffuseCB: HLSL 側と不一致");

    std::shared_ptr<ComputeShader> LoadCS(ID3D11Device* device, const wchar_t* path)
    {
        auto cs = std::make_shared<ComputeShader>();
        if (FAILED(ShaderPath::Load(cs.get(), device, path))) return nullptr;
        return cs;
    }
}

bool WindGrid::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    Shutdown();
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    m_StepCS = LoadCS(device, L"Shader/Wind/WindGridStepCS.hlsl");
    m_DiffuseCS = LoadCS(device, L"Shader/Wind/WindGridDiffuseCS.hlsl");
    if (!m_StepCS || !m_DiffuseCS)
    {
        std::cout << "[WindGrid] shader load failed" << std::endl;
        return false;
    }

    D3D11_TEXTURE3D_DESC td = {};
    td.Width = kX; td.Height = kY; td.Depth = kZ;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    for (int i = 0; i < 2; ++i)
    {
        if (FAILED(device->CreateTexture3D(&td, nullptr, &m_Tex[i]))) return false;
        if (FAILED(device->CreateShaderResourceView(m_Tex[i].Get(), nullptr, &m_SRV[i]))) return false;
        if (FAILED(device->CreateUnorderedAccessView(m_Tex[i].Get(), nullptr, &m_UAV[i]))) return false;
        const float zero[4] = { 0, 0, 0, 0 };
        context->ClearUnorderedAccessViewFloat(m_UAV[i].Get(), zero);
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(Vector4) * 3;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Vector4);
    if (FAILED(device->CreateBuffer(&bd, nullptr, &m_InfoBuffer))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = 3;
    if (FAILED(device->CreateShaderResourceView(m_InfoBuffer.Get(), &sd, &m_InfoSRV))) return false;

    D3D11_TEXTURE3D_DESC st = td;
    st.Height = kReadY;
    st.Usage = D3D11_USAGE_STAGING;
    st.BindFlags = 0;
    st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < kStaging; ++i)
        if (FAILED(device->CreateTexture3D(&st, nullptr, &m_Staging[i]))) return false;
    m_Cpu.assign((size_t)kX * kReadY * kZ * 4, 0);

    m_Ready = true;
    m_PrevValid = false;
    m_CpuValid = false;
    s_Current = this;
    UpdateInfo();
    std::cout << "[WindGrid] " << kX << "x" << kY << "x" << kZ << " ready" << std::endl;
    return true;
}

void WindGrid::Shutdown()
{
    if (s_Current == this) s_Current = nullptr;
    m_Ready = false;
    for (int i = 0; i < 2; ++i) { m_Tex[i].Reset(); m_SRV[i].Reset(); m_UAV[i].Reset(); }
    for (int i = 0; i < kStaging; ++i) { m_Staging[i].Reset(); m_StagingUsed[i] = false; }
    m_InfoBuffer.Reset();
    m_InfoSRV.Reset();
    m_StepCS.reset();
    m_DiffuseCS.reset();
    m_Cpu.clear();
    m_CpuValid = false;
    m_HasCenter = false;
}

void WindGrid::SetCenter(const Vector3& feet, const Vector2& worldWindXZ)
{
    m_Feet = feet;
    m_WorldWind = worldWindXZ;
    m_HasCenter = true;
}

// 読み手が見る情報（原点・大きさ・有効・縁で薄める距離）
void WindGrid::UpdateInfo()
{
    if (!m_InfoBuffer || !m_Context) return;
    const bool on = m_Ready && settings.enabled && m_HasCenter && m_PrevValid;
    Vector4 info[3] = {
        Vector4(m_Origin.x, m_Origin.y, m_Origin.z, settings.cell),
        Vector4((float)kX, (float)kY, (float)kZ, on ? 1.0f : 0.0f),
        Vector4(settings.edgeFade, settings.topFadeCells, m_Feet.y, 0.0f) };
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(m_Context->Map(m_InfoBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        memcpy(m.pData, info, sizeof(info));
        m_Context->Unmap(m_InfoBuffer.Get(), 0);
    }
}

// ============================================================
// 1 歩：原点をプレイヤーの足元に合わせ（マスに揃える）、移流 + モーター + 減衰 → 拡散 × n
// ============================================================
void WindGrid::Step(ID3D11ShaderResourceView* windSources, float dt)
{
    if (!m_Ready || !m_HasCenter) return;
    if (!settings.enabled) { m_PrevValid = false; UpdateInfo(); return; }
    PROFILE_SCOPE_GPU("Wind grid");
    const float cell = (std::max)(settings.cell, 0.25f);
    // 原点 = 足元を真ん中に、マスに揃える（揃えないと毎フレーム値が少しずつ補間されてぼける）
    m_Origin = Vector3(std::floor(m_Feet.x / cell) * cell - kX * 0.5f * cell,
        std::floor((m_Feet.y - settings.feetBelow) / cell) * cell,
        std::floor(m_Feet.z / cell) * cell - kZ * 0.5f * cell);
    if (!m_PrevValid) m_PrevOrigin = m_Origin;

    StepCB cb = {};
    cb.origin = m_Origin;
    cb.cell = cell;
    cb.prevOrigin = m_PrevOrigin;
    cb.dt = (std::min)(dt, 0.1f);
    cb.decay = 1.0f / (std::max)(settings.decayTime, 0.02f);
    cb.relax = settings.relax;
    cb.heightFull = settings.heightFull;
    cb.heightZero = (std::max)(settings.heightZero, settings.heightFull + 0.1f);
    cb.advectWind = m_WorldWind * settings.advectWindShare;
    cb.feetY = m_Feet.y;
    cb.prevValid = m_PrevValid ? 1.0f : 0.0f;
    cb.dims[0] = kX; cb.dims[1] = kY; cb.dims[2] = kZ;
    cb.motorGain = settings.motorGain;

    int cur = m_Cur;
    int next = 1 - cur;
    m_StepCS->WriteBuffer(m_Context, 0, &cb);
    m_StepCS->Bind(m_Context);
    m_StepCS->SetSRV(m_Context, "g_WindSources", windSources);
    m_StepCS->SetSRV(m_Context, "g_Prev", m_SRV[cur].Get());
    m_StepCS->SetUAV(m_Context, "g_Out", m_UAV[next].Get());
    m_StepCS->BindUAVs(m_Context);
    m_Context->Dispatch(kX / 8, kY / 4, kZ / 8);
    m_StepCS->UnbindSRVs(m_Context);
    m_StepCS->UnbindUAVs(m_Context);
    cur = next;

    DiffuseCB dcb = { { kX, kY, kZ }, std::clamp(settings.diffuse, 0.0f, 0.5f) };
    m_DiffuseCS->WriteBuffer(m_Context, 0, &dcb);
    for (int i = 0; i < settings.diffuseIters; ++i)
    {
        next = 1 - cur;
        m_DiffuseCS->Bind(m_Context);
        m_DiffuseCS->SetSRV(m_Context, "g_In", m_SRV[cur].Get());
        m_DiffuseCS->SetUAV(m_Context, "g_Out", m_UAV[next].Get());
        m_DiffuseCS->BindUAVs(m_Context);
        m_Context->Dispatch(kX / 8, kY / 4, kZ / 8);
        m_DiffuseCS->UnbindSRVs(m_Context);
        m_DiffuseCS->UnbindUAVs(m_Context);
        cur = next;
    }
    m_Cur = cur;
    m_PrevOrigin = m_Origin;
    m_PrevValid = true;
    UpdateInfo();

    if (settings.cpuReadback)
    {
        ReadCPU();
        CopyForCPU();
    }
}

void WindGrid::BindTo(Shader& shader, ID3D11DeviceContext* context) const
{
    shader.SetSRV(context, "g_WindGrid", m_Ready ? m_SRV[m_Cur].Get() : nullptr);
    shader.SetSRV(context, "g_WindGridInfo", m_Ready ? m_InfoSRV.Get() : nullptr);
}

void WindGrid::BindCurrent(Shader& shader, ID3D11DeviceContext* context)
{
    if (s_Current) { s_Current->BindTo(shader, context); return; }
    shader.SetSRV(context, "g_WindGrid", nullptr);
    shader.SetSRV(context, "g_WindGridInfo", nullptr);
}

// ============================================================
// CPU へ：下 kReadY 層を staging へ写し（3 枚の輪転）、一番古い物を待たずに読む
// ============================================================
void WindGrid::CopyForCPU()
{
    const int i = m_StagingNext;
    D3D11_BOX box = { 0, 0, 0, (UINT)kX, (UINT)kReadY, (UINT)kZ };
    m_Context->CopySubresourceRegion(m_Staging[i].Get(), 0, 0, 0, 0, m_Tex[m_Cur].Get(), 0, &box);
    m_StagingOrigin[i] = m_Origin;
    m_StagingUsed[i] = true;
    m_StagingNext = (i + 1) % kStaging;
}

void WindGrid::ReadCPU()
{
    const int i = m_StagingNext;   // 次に書く枠 = 一番古い物
    if (!m_StagingUsed[i]) return;
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(m_Context->Map(m_Staging[i].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) return;
    for (int z = 0; z < kZ; ++z)
        for (int y = 0; y < kReadY; ++y)
        {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(
                static_cast<const uint8_t*>(m.pData) + (size_t)z * m.DepthPitch + (size_t)y * m.RowPitch);
            memcpy(&m_Cpu[(((size_t)z * kReadY + y) * kX) * 4], row, sizeof(uint16_t) * 4 * kX);
        }
    m_Context->Unmap(m_Staging[i].Get(), 0);
    m_CpuOrigin = m_StagingOrigin[i];
    m_CpuValid = true;
}

Vector3 WindGrid::SampleCPU(const Vector3& p, float& weight) const
{
    weight = 0.0f;
    if (!m_CpuValid || !settings.enabled || m_Cpu.empty()) return Vector3::Zero;
    const float cell = (std::max)(settings.cell, 0.25f);
    const Vector3 f = (p - m_CpuOrigin) / cell;
    const float edge = (std::max)(settings.edgeFade / cell, 1e-3f);
    const float wx = std::clamp((std::min)(f.x, kX - f.x) / edge, 0.0f, 1.0f);
    const float wz = std::clamp((std::min)(f.z, kZ - f.z) / edge, 0.0f, 1.0f);
    const float wy = std::clamp(((float)kReadY - f.y) / (std::max)(settings.topFadeCells, 1e-3f), 0.0f, 1.0f);
    weight = wx * wz * wy;
    if (weight <= 0.0f) return Vector3::Zero;

    const float fx = f.x - 0.5f, fy = f.y - 0.5f, fz = f.z - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy), z0 = (int)std::floor(fz);
    const float tx = fx - x0, ty = fy - y0, tz = fz - z0;
    auto at = [&](int x, int y, int z)
        {
            x = std::clamp(x, 0, kX - 1); y = std::clamp(y, 0, kReadY - 1); z = std::clamp(z, 0, kZ - 1);
            const uint16_t* c = &m_Cpu[(((size_t)z * kReadY + y) * kX + x) * 4];
            return Vector3(DirectX::PackedVector::XMConvertHalfToFloat(c[0]), DirectX::PackedVector::XMConvertHalfToFloat(c[1]), DirectX::PackedVector::XMConvertHalfToFloat(c[2]));
        };
    auto lerp3 = [](const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; };
    const Vector3 x00 = lerp3(at(x0, y0, z0), at(x0 + 1, y0, z0), tx);
    const Vector3 x10 = lerp3(at(x0, y0 + 1, z0), at(x0 + 1, y0 + 1, z0), tx);
    const Vector3 x01 = lerp3(at(x0, y0, z0 + 1), at(x0 + 1, y0, z0 + 1), tx);
    const Vector3 x11 = lerp3(at(x0, y0 + 1, z0 + 1), at(x0 + 1, y0 + 1, z0 + 1), tx);
    return lerp3(lerp3(x00, x10, ty), lerp3(x01, x11, ty), tz);
}

Vector3 WindGrid::BlendCPU(const Vector3& formula, const Vector3& p) const
{
    float w = 0.0f;
    const Vector3 g = SampleCPU(p, w);
    if (w <= 0.0f) return formula;
    const Vector3 pick = (formula.LengthSquared() >= g.LengthSquared()) ? formula : g;   // WindGridBlend と同じ：強い方
    return formula + (pick - formula) * w;
}

void WindGrid::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Wind Grid (God of War style, 3D)")) return;
    ImGui::PushID("windgrid");
    ImGui::Checkbox("Enabled", &settings.enabled);
    ImGui::SameLine();
    if (ImGui::Button("Clear (drop the lingering wind)")) Reset();
    ImGui::Text("%dx%dx%d, origin (%.0f, %.0f, %.0f)  cpu %s", kX, kY, kZ, m_Origin.x, m_Origin.y, m_Origin.z, m_CpuValid ? "ok" : "-");
    ImGui::DragFloat("Linger (decay time, s)", &settings.decayTime, 0.01f, 0.02f, 5.0f);
    ImGui::DragFloat("Follow sources (relax 1/s)", &settings.relax, 0.5f, 0.0f, 100.0f);
    ImGui::DragFloat("Source gain", &settings.motorGain, 0.01f, 0.0f, 3.0f);
    ImGui::SliderInt("Diffuse iterations", &settings.diffuseIters, 0, 8);
    ImGui::DragFloat("Diffuse", &settings.diffuse, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Full up to (m above feet)", &settings.heightFull, 0.05f, 0.0f, 8.0f);
    ImGui::DragFloat("Zero at (m above feet)", &settings.heightZero, 0.05f, 0.1f, 12.0f);
    ImGui::DragFloat("Carried by weather wind x", &settings.advectWindShare, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Edge fade (m)", &settings.edgeFade, 0.1f, 0.0f, 30.0f);
    ImGui::Checkbox("CPU readback (cape, tumbleweeds, F8)", &settings.cpuReadback);
    ImGui::PopID();
}
