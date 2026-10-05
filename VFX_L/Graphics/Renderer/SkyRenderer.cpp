// ============================================================
// SkyRenderer.cpp
// ============================================================
#include "Graphics/Renderer/SkyRenderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Camera/CameraBase.h"
#include <iostream>

using namespace DirectX::SimpleMath;

bool SkyRenderer::Initialize(ID3D11Device* device)
{
    m_VS = std::make_shared<VertexShader>();
    m_PS = std::make_shared<PixelShader>();
    const bool ok = SUCCEEDED(ShaderPath::Load(m_VS.get(), device, L"Shader/Sky/SkyVS.hlsl"))
                 && SUCCEEDED(ShaderPath::Load(m_PS.get(), device, L"Shader/Sky/SkyPS.hlsl"));
    std::cout << "[SkyRenderer] " << (ok ? "OK" : "FAILED (no sky, clear color only)") << std::endl;
    if (!ok) { m_VS.reset(); m_PS.reset(); }
    return ok;
}

// ============================================================
// 画面全体を空で塗る（シーンの描画の一番最初）。
// 深度は読まず書かない：後から描く物は全部この上に乗る
// ============================================================
void SkyRenderer::Render(ID3D11DeviceContext* context, CameraBase* camera, const Params& p) const
{
    if (!m_VS || !m_PS || !context || !camera) return;

    SkyCB cb = {};
    cb.invViewProj = (camera->GetViewMatrix() * camera->GetProjectionMatrix()).Invert();
    cb.cameraPos = camera->GetPosition();
    cb.zenithCurve = p.zenithCurve;
    cb.zenith = p.zenith;
    cb.sunGlow = p.sunGlow;
    cb.horizon = p.horizon;
    cb.sunGlowPower = p.sunGlowPower;
    cb.below = p.below;
    cb.sunDisk = p.sunDisk;
    cb.sunDir = p.sunDir;
    cb.sunColor = p.sunColor;
    m_PS->WriteBuffer(context, 0, &cb);

    m_VS->Bind(context);
    m_PS->Bind(context);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(rs.DepthNone(), 0);
    context->RSSetState(rs.CullNone());

    context->Draw(3, 0);

    rs.Restore(context);
}
