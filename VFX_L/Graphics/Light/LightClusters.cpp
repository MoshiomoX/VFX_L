// ============================================================
// LightClusters.cpp
// ============================================================
#include "Graphics/Light/LightClusters.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Camera/CameraBase.h"
#include "imgui.h"

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector4;

bool LightClusters::Initialize(ID3D11Device* device, uint32_t maxLights)
{
    if (!device) return false;
    m_Words = (maxLights + 31u) / 32u;

    m_CS = std::make_shared<ComputeShader>();
    if (FAILED(ShaderPath::Load(m_CS.get(), device, L"Shader/Light/LightClusterCS.hlsl")))
    {
        std::cout << "[Error] LightClusters: LightClusterCS load failed (point lights fall back to the full loop)" << std::endl;
        m_CS.reset();
        return false;
    }

    // クラスタごとのビット（CS が書き、PS が読む）
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(uint32_t) * kCount * m_Words;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(uint32_t);
    if (FAILED(device->CreateBuffer(&bd, nullptr, &m_MaskBuffer))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = kCount * m_Words;
    if (FAILED(device->CreateShaderResourceView(m_MaskBuffer.Get(), &sd, &m_MaskSRV))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = kCount * m_Words;
    if (FAILED(device->CreateUnorderedAccessView(m_MaskBuffer.Get(), &ud, &m_MaskUAV))) return false;

    // カメラと分割の情報（1 要素。CPU が毎フレーム書き、CS と PS が読む）
    D3D11_BUFFER_DESC ib = {};
    ib.ByteWidth = sizeof(LightClusterInfo);
    ib.Usage = D3D11_USAGE_DEFAULT;
    ib.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ib.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    ib.StructureByteStride = sizeof(LightClusterInfo);
    LightClusterInfo zero = {};
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = &zero;
    if (FAILED(device->CreateBuffer(&ib, &init, &m_InfoBuffer))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC isd = {};
    isd.Format = DXGI_FORMAT_UNKNOWN;
    isd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    isd.Buffer.NumElements = 1;
    if (FAILED(device->CreateShaderResourceView(m_InfoBuffer.Get(), &isd, &m_InfoSRV))) return false;

    // TEMP-TEST: 前のやり方（全部の光を回す）と見比べる
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_NO_CLUSTER", env, sizeof(env)) > 0 && env[0] == '1')
        m_Settings.enabled = false;
    if (GetEnvironmentVariableA("VFXL_CLUSTER_HEAT", env, sizeof(env)) > 0 && env[0] == '1')
        m_Settings.debugView = true;

    std::cout << "[OK] LightClusters: " << kX << "x" << kY << "x" << kZ << " clusters, "
              << m_Words << " mask words each" << std::endl;
    return true;
}

void LightClusters::Shutdown()
{
    m_CS.reset();
    m_MaskUAV.Reset(); m_MaskSRV.Reset(); m_MaskBuffer.Reset();
    m_InfoSRV.Reset(); m_InfoBuffer.Reset();
    m_Built = false;
}

void LightClusters::Build(ID3D11DeviceContext* ctx, CameraBase& camera,
    ID3D11ShaderResourceView* lights, ID3D11ShaderResourceView* count)
{
    m_Built = false;
    if (!m_Settings.enabled || !ctx || !m_CS || !m_MaskUAV || !lights || !count) return;

    // 描画と同じ行列（カメラの揺れ込み）。PS は view * proj で自分のクラスタを決め、
    // CS は同じ view と proj の倍率でクラスタの箱を作る = 両方が必ず同じ分け方になる
    const Matrix view = camera.GetViewMatrix();
    const Matrix proj = camera.GetProjectionMatrix();
    const Matrix vp = view * proj;

    LightClusterInfo info = {};
    for (int r = 0; r < 4; ++r)
    {
        info.viewProj[r] = Vector4(vp.m[r][0], vp.m[r][1], vp.m[r][2], vp.m[r][3]);
        info.view[r] = Vector4(view.m[r][0], view.m[r][1], view.m[r][2], view.m[r][3]);
    }
    info.projScale = Vector2(proj._11, proj._22);
    const float zn = (std::max)(m_Settings.zNear, 0.05f);
    const float zf = (std::max)(m_Settings.zFar, zn * 1.5f);
    info.zNear = zn;
    info.zFar = zf;
    info.sliceScale = (float)kZ / std::log(zf / zn);
    info.sliceBias = -std::log(zn) * info.sliceScale;
    info.enabled = 1;
    info.debugView = m_Settings.debugView ? 1u : 0u;
    ctx->UpdateSubresource(m_InfoBuffer.Get(), 0, nullptr, &info, 0, 0);

    UnbindPS(ctx);   // PS の t10 に残っていたら CS の UAV と重なる（HAZARD）
    m_CS->Bind(ctx);
    m_CS->SetSRV(ctx, "g_Lights", lights);
    m_CS->SetSRV(ctx, "g_LightCount", count);
    m_CS->SetSRV(ctx, "g_Info", m_InfoSRV.Get());
    m_CS->SetUAV(ctx, "g_Masks", m_MaskUAV.Get());
    m_CS->BindUAVs(ctx);
    ctx->Dispatch((kCount + 63u) / 64u, 1, 1);
    m_CS->UnbindSRVs(ctx);
    m_CS->UnbindUAVs(ctx);
    m_Built = true;
}

void LightClusters::BindPS(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    ID3D11ShaderResourceView* srvs[2] = { nullptr, nullptr };
    if (m_Built)
    {
        srvs[0] = m_MaskSRV.Get();
        srvs[1] = m_InfoSRV.Get();
    }
    ctx->PSSetShaderResources(kMaskSlot, 2, srvs);
}

void LightClusters::UnbindPS(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    ID3D11ShaderResourceView* nulls[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(kMaskSlot, 2, nulls);
}

void LightClusters::DrawImGui()
{
    ImGui::Checkbox("Clustered (Forward+)##lc", &m_Settings.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Heat map##lc", &m_Settings.debugView);
    ImGui::DragFloatRange2("Slices near / far (m)##lc", &m_Settings.zNear, &m_Settings.zFar, 0.5f, 0.1f, 2000.0f);
    ImGui::TextDisabled("%u x %u x %u clusters, %u mask words each%s", kX, kY, kZ, m_Words,
        m_Built ? "" : "  (not built this frame: full loop)");
    ImGui::TextDisabled("heat map: lights a pixel visits, blue 1 / green 4 / red 16+");
}
