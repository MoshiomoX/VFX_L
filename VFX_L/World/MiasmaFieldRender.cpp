// ============================================================
// MiasmaFieldRender.cpp
// 瘴気の描画（半分の解像度で光線を進める → 深度を見ながら全解像度へ広げて HDR に重ねる）とパネル
// ============================================================
#include "World/MiasmaField.h"
#include "Graphics/Graphics.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Camera/CameraBase.h"
#include "imgui.h"

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

// 半分の解像度の RGBA16F（大きさが変わったら作り直す）
bool MiasmaField::EnsureTargets(ID3D11Device* device, UINT width, UINT height)
{
    const UINT hw = (std::max)(1u, (width + 1) / 2), hh = (std::max)(1u, (height + 1) / 2);
    if (m_HalfTex && m_HalfW == hw && m_HalfH == hh) return true;
    m_HalfTex.Reset(); m_HalfRTV.Reset(); m_HalfSRV.Reset();
    if (!device || width == 0 || height == 0) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = hw;
    td.Height = hh;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &m_HalfTex))
        || FAILED(device->CreateRenderTargetView(m_HalfTex.Get(), nullptr, &m_HalfRTV))
        || FAILED(device->CreateShaderResourceView(m_HalfTex.Get(), nullptr, &m_HalfSRV)))
    {
        m_HalfTex.Reset(); m_HalfRTV.Reset(); m_HalfSRV.Reset();
        return false;
    }
    m_HalfW = hw;
    m_HalfH = hh;
    return true;
}

void MiasmaField::Render(ID3D11DeviceContext* context, Graphics& graphics, CameraBase* camera, ID3D11ShaderResourceView* height)
{
    if (!Active() || !m_HasOrigin || !context || !camera || !height || !m_MarchPS || !m_CompositePS) return;
    ID3D11ShaderResourceView* depth = graphics.GetDepthSRV();
    ID3D11RenderTargetView* sceneRTV = graphics.GetSceneRTV();
    if (!depth || !sceneRTV || graphics.GetSampleCount() <= 1) return;   // シェーダーは MSAA の深度だけ読む
    const UINT fw = (UINT)graphics.GetWidth(), fh = (UINT)graphics.GetHeight();
    if (!EnsureTargets(graphics.GetDevice(), fw, fh)) return;

    const auto& s = m_Settings;
    const Matrix view = camera->GetViewMatrix();
    const Matrix proj = camera->GetProjectionMatrix();
    MarchCB mcb = {};
    mcb.invViewProj = (view * proj).Invert();
    mcb.camPos = camera->GetPosition();
    mcb.maxDist = s.maxDist;
    mcb.origin = m_Origin;
    mcb.mapOrigin = m_MapOrigin;
    mcb.mapSize = m_MapSize;
    mcb.fullSize = Vector2((float)fw, (float)fh);
    mcb.yMin = m_YMin;
    mcb.yMax = m_YMax;
    mcb.extinction = s.extinction;
    mcb.steps = (float)(std::max)(s.steps, 4);
    mcb.colorLow = s.colorLow;
    mcb.bands = s.bands;
    mcb.colorHigh = s.colorHigh;
    mcb.bandSoft = s.bandSoft;
    mcb.topBlend = s.topBlend;
    mcb.glow = s.glow;
    mcb.edgeFade = s.edgeFade;
    mcb.detailOffset = m_DetailOffset;
    mcb.detailScale = s.detailScale;
    mcb.erode = (s.detail && m_NoiseSRV) ? s.erode : 0.0f;
    mcb.densRef = s.densRef;
    mcb.detailScale2 = s.detailScale2;
    mcb.detail2Mix = s.detail2Mix;
    mcb.torchColor = s.torchColor;
    mcb.torchCount = (float)m_TorchCount;
    mcb.torchGlow = s.torchGlow;
    mcb.torchGlowRadius = s.torchGlowRadius;
    for (int i = 0; i < m_TorchCount; ++i) mcb.torch[i] = m_Torch[i];
    m_MarchPS->WriteBuffer(context, 0, &mcb);

    CompositeCB ccb = {};
    ccb.depthA = proj._33 * proj._34;   // アウトラインと同じ「深度 → 目からの距離」
    ccb.depthB = proj._43;
    ccb.tolerance = s.upsampleDepth;
    ccb.halfSize = Vector2((float)m_HalfW, (float)m_HalfH);
    ccb.fullSize = Vector2((float)fw, (float)fh);
    m_CompositePS->WriteBuffer(context, 0, &ccb);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetDepthStencilState(rs.DepthNone(), 0);
    context->RSSetState(rs.CullNone());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    m_VS->Bind(context);

    // 1) 半分の解像度で光線（深度を読むので DSV は外す）
    ID3D11RenderTargetView* half = m_HalfRTV.Get();
    context->OMSetRenderTargets(1, &half, nullptr);
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)m_HalfW, (float)m_HalfH, 0.0f, 1.0f };
    context->RSSetViewports(1, &vp);
    context->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    m_MarchPS->Bind(context);
    m_MarchPS->SetSRV(context, 0u, depth);
    m_MarchPS->SetSRV(context, 1u, m_VolSRV[m_Cur].Get());
    m_MarchPS->SetSRV(context, 2u, height);
    m_MarchPS->SetSRV(context, 3u, m_NoiseSRV.Get());
    ID3D11SamplerState* samplers[2] = { rs.LinearClamp(), rs.LinearWrap() };
    context->PSSetSamplers(0, 2, samplers);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* none[4] = { nullptr, nullptr, nullptr, nullptr };
    context->PSSetShaderResources(0, 4, none);

    // 2) 全解像度へ広げてシーンに重ねる
    graphics.RestoreRenderTarget();   // 画面の大きさのビューポートに戻す
    context->OMSetRenderTargets(1, &sceneRTV, nullptr);
    context->OMSetBlendState(m_Blend.Get(), blendFactor, 0xFFFFFFFF);
    m_CompositePS->Bind(context);
    m_CompositePS->SetSRV(context, 0u, depth);
    m_CompositePS->SetSRV(context, 1u, m_HalfSRV.Get());
    context->Draw(3, 0);
    context->PSSetShaderResources(0, 2, none);   // 深度を次に DSV として使う前に外す

    rs.Restore(context);
    graphics.RestoreRenderTarget();
}

void MiasmaField::DrawImGui()
{
    if (!ImGui::CollapsingHeader(m_Kind == Kind::Dust ? "Sand Dust (desert, 3D)###miasma" : "Miasma (ruins, 3D)###miasma")) return;
    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##miasma", &s.enabled);
    ImGui::Text("active %d  box %.1f, %.1f  y %.1f..%.1f", Active() ? 1 : 0, m_Origin.x, m_Origin.y, m_YMin, m_YMax);
    if (ImGui::Button("Refill##miasma")) m_NeedReset = true;
    ImGui::SameLine();
    if (ImGui::Button("Clear around player##miasma")) Clear(m_Player, 8.0f);
    ImGui::SeparatorText("Motion");
    ImGui::DragFloat("Base Density", &s.base, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("Layer Height (m)", &s.layerH, 0.02f, 0.2f, 6.0f);
    ImGui::DragFloat("Patch Scale (1/m)", &s.noiseScale, 0.002f, 0.005f, 0.5f, "%.3f");
    ImGui::DragFloat("Hollow Bias", &s.lowBias, 0.02f, 0.0f, 5.0f);
    ImGui::DragFloat("Regrow (1/s)", &s.regrow, 0.005f, 0.0f, 2.0f, "%.3f");
    ImGui::DragFloat("Decay (1/s)", &s.decay, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("Settle (m/s)", &s.settle, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("World Wind Share", &s.windShare, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Area Radius x", &s.sourceMul, 0.01f, 0.2f, 3.0f);
    ImGui::DragFloat("Burn Rate (1/s)", &s.burnRate, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Blast Push (m/s)", &s.blastPush, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Blast Clear (1/s)", &s.blastClear, 0.05f, 0.0f, 30.0f);
    ImGui::DragFloat("Player Radius (m)", &s.playerRadius, 0.02f, 0.0f, 5.0f);
    ImGui::DragFloat("Player Push (m/s)", &s.playerPush, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Wall Height (m)", &s.wallHeight, 0.05f, 0.0f, kTop);
    ImGui::DragFloat("Lift Rate (blowing sand)", &s.liftRate, 0.05f, 0.0f, 30.0f);
    ImGui::DragFloat("Blast Dust (/s)", &s.blastDust, 0.05f, 0.0f, 30.0f);
    ImGui::DragFloat("Lift Height (m)", &s.liftHeight, 0.02f, 0.1f, kTop);
    ImGui::SeparatorText("Look");
    ImGui::DragFloat("Extinction (1/m)", &s.extinction, 0.01f, 0.0f, 10.0f);
    ImGui::DragInt("Steps", &s.steps, 1.0f, 4, 128);
    ImGui::DragFloat("Max Distance (m)", &s.maxDist, 0.5f, 5.0f, 200.0f);
    ImGui::DragFloat("Edge Fade (m)", &s.edgeFade, 0.1f, 0.1f, 30.0f);
    ImGui::DragFloat("Toon Bands", &s.bands, 0.05f, 1.0f, 8.0f);
    ImGui::DragFloat("Band Softness", &s.bandSoft, 0.005f, 0.0f, 0.5f, "%.3f");
    ImGui::ColorEdit3("Color Low", &s.colorLow.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit3("Color High", &s.colorHigh.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::DragFloat("Top Blend (m)", &s.topBlend, 0.05f, 0.1f, kTop);
    ImGui::DragFloat("Band Glow", &s.glow, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Upsample Depth Tol", &s.upsampleDepth, 0.002f, 0.002f, 0.5f, "%.3f");
    ImGui::SeparatorText("Detail noise (breaks the edges into wisps)");
    ImGui::Checkbox("Detail Noise", &s.detail);
    ImGui::DragFloat("Detail Scale (1/m)", &s.detailScale, 0.005f, 0.02f, 2.0f, "%.3f");
    ImGui::DragFloat("Detail Scale 2 (1/m)", &s.detailScale2, 0.01f, 0.05f, 4.0f, "%.3f");
    ImGui::SliderFloat("Detail 2 Mix", &s.detail2Mix, 0.0f, 1.0f);
    ImGui::SliderFloat("Erode", &s.erode, 0.0f, 1.0f);
    ImGui::DragFloat("Core Density", &s.densRef, 0.01f, 0.05f, 4.0f);
    ImGui::DragFloat("Detail Rise (m/s)", &s.detailRise, 0.01f, 0.0f, 3.0f);
    ImGui::SeparatorText("Torches (burn a clearing, warm glow)");
    ImGui::Checkbox("Torches##miasma", &s.torches);
    ImGui::Text("torches in the box: %d", m_TorchCount);
    ImGui::DragFloat("Torch Clear Radius (m)", &s.torchRadius, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Torch Burn (1/s)", &s.torchBurn, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Torch Lift (m/s)", &s.torchLift, 0.02f, 0.0f, 5.0f);
    ImGui::DragFloat("Torch Glow", &s.torchGlow, 0.02f, 0.0f, 10.0f);
    ImGui::DragFloat("Torch Glow Radius (m)", &s.torchGlowRadius, 0.05f, 0.5f, 20.0f);
    ImGui::ColorEdit3("Torch Color", &s.torchColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
}
