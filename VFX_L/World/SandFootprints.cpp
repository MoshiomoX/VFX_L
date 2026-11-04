// ============================================================
// SandFootprints.cpp
// ============================================================
#include "World/SandFootprints.h"
#include "World/LocalWind.h"
#include "World/WindState.h"
#include "Camera/CameraBase.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    // SandFootprintVS / PS の b0 と同じ並び（128B）
    struct FootCB
    {
        Matrix viewProj;
        Vector2 mapOrigin, mapSize;
        float strength, wetMul, hasState, raise;
        Vector3 sunDir; float depth;
        float berm, grain, sunGain, floorDark;
    };
    static_assert(sizeof(FootCB) == 128, "FootCB: HLSL 側と不一致");
}

bool SandFootprints::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    m_VS = std::make_shared<VertexShader>();
    if (FAILED(ShaderPath::Load(m_VS.get(), device, L"Shader/Sand/SandFootprintVS.hlsl"))) m_VS.reset();
    m_PS = std::make_shared<PixelShader>();
    if (FAILED(ShaderPath::Load(m_PS.get(), device, L"Shader/Sand/SandFootprintPS.hlsl"))) m_PS.reset();

    // 掛け算：結果 = 下の色 × PS の出力（1 = そのまま。浮動小数の RT なので 1 より大きい = 明るくできる）。
    // 下の砂の色とトゥーンの陰影がそのまま残る
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&bd, &m_Multiply))) m_Multiply.Reset();
    return m_VS && m_PS && m_Multiply && CreateBuffer(m_Settings.maxPrints);
}

void SandFootprints::Shutdown()
{
    m_Prints.clear();
    m_Multiply.Reset();
    m_SRV.Reset();
    m_Buffer.Reset();
    m_VS.reset();
    m_PS.reset();
    m_BufCount = 0;
}

bool SandFootprints::CreateBuffer(int count)
{
    m_SRV.Reset();
    m_Buffer.Reset();
    m_BufCount = 0;
    if (!m_Device || count <= 0) return false;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(GpuPrint) * count);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(GpuPrint);
    if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &m_Buffer))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_UNKNOWN;
    sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.NumElements = (UINT)count;
    if (FAILED(m_Device->CreateShaderResourceView(m_Buffer.Get(), &sv, &m_SRV))) { m_Buffer.Reset(); return false; }
    m_BufCount = count;
    return true;
}

void SandFootprints::Add(const Vector3& pos, float yawDeg, int side, const Vector3& normal)
{
    if (!m_Settings.enabled) return;
    Print p;
    p.pos = pos;
    p.normal = normal;
    p.normal.Normalize();
    p.yaw = DirectX::XMConvertToRadians(yawDeg);
    p.side = side;
    m_Prints.push_back(p);
    const size_t cap = (size_t)(std::max)(m_Settings.maxPrints, 1);
    if (m_Prints.size() > cap)
        m_Prints.erase(m_Prints.begin(), m_Prints.begin() + (m_Prints.size() - cap));   // 古い方から
}

void SandFootprints::Update(float dt, const LocalWind* local, const WindState* world, float time,
    float liftWind, float range, float worldShare)
{
    const auto& s = m_Settings;
    const float rangeInv = 1.0f / (std::max)(range, 0.1f);
    for (Print& p : m_Prints)
    {
        p.age += dt;
        // SandFieldCS の blow と同じ（乾き具合は CPU では分からないので VS が状態を読んで薄くする）
        float blow = 0.0f;
        if (local)
        {
            const Vector3 a = local->Air(p.pos.x, p.pos.z);
            blow = std::clamp((std::sqrt(a.x * a.x + a.z * a.z) - liftWind) * rangeInv, 0.0f, 1.0f);
        }
        if (world)
        {
            const Vector3 w = world->Velocity(time, p.pos.x, p.pos.z);
            const float bw = std::clamp((std::sqrt(w.x * w.x + w.z * w.z) - liftWind) * rangeInv, 0.0f, 1.0f) * worldShare;
            blow = (std::max)(blow, bw);
        }
        p.erode = (std::min)(1.0f, p.erode + blow * s.windErode * dt);
    }
    m_Prints.erase(std::remove_if(m_Prints.begin(), m_Prints.end(),
        [&](const Print& p) { return p.age >= s.life || p.erode >= 1.0f; }), m_Prints.end());
}

void SandFootprints::Render(CameraBase& camera, ID3D11ShaderResourceView* state,
    const Vector2& mapOrigin, const Vector2& mapSize, const Vector3& sunDir)
{
    const auto& s = m_Settings;
    ID3D11DeviceContext* ctx = m_Context;
    if (!s.enabled || m_Prints.empty() || !ctx || !m_VS || !m_PS || !m_Multiply) return;
    if (s.maxPrints != m_BufCount && !CreateBuffer(s.maxPrints)) return;

    // 年と風で薄くした濃さを詰める
    m_Upload.clear();
    for (const Print& p : m_Prints)
    {
        const float lifeK = std::clamp((s.life - p.age) / (std::max)(s.fadeTime, 0.01f), 0.0f, 1.0f);
        const float bornK = std::clamp(p.age / 0.08f, 0.0f, 1.0f);   // 踏んだ瞬間にふっと付く
        const float a = lifeK * bornK * (1.0f - p.erode);
        if (a <= 0.003f) continue;
        GpuPrint g = {};
        g.pos = p.pos;
        g.yaw = p.yaw;
        g.normal = p.normal;
        g.alpha = a;
        g.side = p.side ? -1.0f : 1.0f;   // 左足は横を反転（土踏まずが内側）
        g.length = s.length;
        g.width = s.width;
        m_Upload.push_back(g);
        if ((int)m_Upload.size() >= m_BufCount) break;
    }
    if (m_Upload.empty()) return;

    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(m_Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, m_Upload.data(), sizeof(GpuPrint) * m_Upload.size());
    ctx->Unmap(m_Buffer.Get(), 0);

    FootCB cb = {};
    cb.viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
    cb.mapOrigin = mapOrigin;
    cb.mapSize = mapSize;
    cb.strength = std::clamp(s.strength, 0.0f, 1.0f);
    cb.wetMul = s.wetMul;
    cb.hasState = state ? 1.0f : 0.0f;
    cb.raise = s.lift;
    cb.sunDir = sunDir;
    if (cb.sunDir.LengthSquared() < 1e-6f) cb.sunDir = Vector3(0.0f, 1.0f, 0.0f);
    cb.sunDir.Normalize();
    cb.depth = s.depth;
    cb.berm = s.berm;
    cb.grain = s.grain;
    cb.sunGain = s.sunGain;
    cb.floorDark = s.floorDark;
    m_VS->WriteBuffer(ctx, 0, &cb);
    m_PS->WriteBuffer(ctx, 0, &cb);
    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    m_VS->SetSRV(ctx, "g_Prints", m_SRV.Get());
    if (state) m_VS->SetSRV(ctx, "g_State", state);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(m_Multiply.Get(), blendFactor, 0xFFFFFFFF);   // 掛け算：下の砂の色 × PS の出力
    ctx->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    ctx->RSSetState(rs.CullNone());
    ctx->DrawInstanced(6, (UINT)m_Upload.size(), 0, 0);

    // 状態のテクスチャは次のフレームの SandFieldCS が UAV にするので外す
    m_VS->UnbindSRVs(ctx);
    rs.Restore(ctx);
}

void SandFootprints::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Sand Footprints (player)")) return;
    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##foot", &s.enabled);
    ImGui::SameLine();
    ImGui::Text("prints %d", Count());
    ImGui::DragFloat("Length (m)##foot", &s.length, 0.005f, 0.05f, 1.0f);
    ImGui::DragFloat("Width (m)##foot", &s.width, 0.005f, 0.03f, 0.6f);
    ImGui::SliderFloat("Strength##foot", &s.strength, 0.0f, 1.0f);
    ImGui::DragFloat("Dent depth (m)##foot", &s.depth, 0.001f, 0.0f, 0.2f);
    ImGui::DragFloat("Rim height (m)##foot", &s.berm, 0.001f, 0.0f, 0.1f);
    ImGui::DragFloat("Sun on walls##foot", &s.sunGain, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Floor dark##foot", &s.floorDark, 0.005f, 0.0f, 0.8f);
    ImGui::DragFloat("Grain##foot", &s.grain, 0.01f, 0.0f, 1.5f);
    ImGui::DragFloat("Life (s)##foot", &s.life, 0.1f, 1.0f, 120.0f);
    ImGui::DragFloat("Fade time (s)##foot", &s.fadeTime, 0.05f, 0.1f, 30.0f);
    ImGui::DragFloat("Wind erode (1/s)##foot", &s.windErode, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Wet x##foot", &s.wetMul, 0.01f, 1.0f, 4.0f);
    ImGui::DragFloat("Raise (m)##foot", &s.lift, 0.001f, 0.0f, 0.3f);
    ImGui::SliderInt("Max prints##foot", &s.maxPrints, 8, 1024);
    if (ImGui::Button("Clear##foot")) Clear();
}
