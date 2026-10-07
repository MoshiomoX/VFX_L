// ============================================================
// Outline.cpp
// ============================================================
#include "Graphics/PostProcess/Outline.h"
#include "Core/GameSettings.h"
#include "Graphics/Graphics.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Camera/CameraBase.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

bool Outline::Initialize(ID3D11Device* device)
{
    m_VS = std::make_shared<VertexShader>();
    m_PS = std::make_shared<PixelShader>();
    // 全画面の三角形は空と同じ物を使う
    bool ok = SUCCEEDED(ShaderPath::Load(m_VS.get(), device, L"Shader/Sky/SkyVS.hlsl"))
           && SUCCEEDED(ShaderPath::Load(m_PS.get(), device, L"Shader/PostProcess/OutlinePS.hlsl"));

    // 乗算済みアルファ：結果 = 出力 + 下の色 × (1 − a)。PS は「線の色 × k, k」を出す = 下の色を線の色へ k だけ寄せる
    // （黒に近い tint なら 10-04 の乗算とほぼ同じ見え方。陣営の青 / 赤はこれでないと出せない）
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bd, &m_Blend));

    std::cout << "[Outline] " << (ok ? "OK" : "FAILED (no outlines)") << std::endl;
    if (!ok) { m_VS.reset(); m_PS.reset(); m_Blend.Reset(); }
    return ok;
}

// ============================================================
// シーンの不透明な物の後に 1 回。深度を読むので DSV を外して描き、元に戻す
// ============================================================
void Outline::Render(ID3D11DeviceContext* context, Graphics& graphics, CameraBase* camera)
{
    // enabled = デバッグ用の開閉（ImGui / VFXL_NO_OUTLINE）、GameSettings::outline = プレイヤーの設定（一時停止メニューの「設定」）
    if (!m_Params.enabled || !GameSettings::Get().outline) return;
    if (!m_VS || !m_PS || !m_Blend || !context || !camera) return;
    ID3D11ShaderResourceView* depth = graphics.GetDepthSRV();
    ID3D11ShaderResourceView* stencil = graphics.GetStencilSRV();
    ID3D11RenderTargetView* rtv = graphics.GetSceneRTV();
    if (!depth || !stencil || !rtv || graphics.GetSampleCount() <= 1) return;   // シェーダーは MSAA の深度だけ読む

    // 投影行列から「深度 → 目からの距離」：eye = B / (d - A)、A = _33 × _34、B = _43
    const Matrix proj = camera->GetProjectionMatrix();
    OutlineCB cb = {};
    cb.depthA = proj._33 * proj._34;
    cb.depthB = proj._43;
    cb.thickness = (std::max)(1.0f, std::round(m_Params.thickness * graphics.GetHeight() / 1080.0f));
    cb.threshold = m_Params.threshold;
    cb.fadeStart = m_Params.fadeStart;
    cb.fadeEnd = m_Params.fadeEnd;
    cb.strength = m_Params.strength;
    cb.debugView = m_Params.debugView ? 1.0f : 0.0f;
    cb.tint = m_Params.tint;
    // 陣営の線（ステンシル 1 = 味方、2 = 敵）。切っている時は濃さ 0 = 全部 tint の線
    cb.factionStrength = m_Params.factionEnabled ? m_Params.factionStrength : 0.0f;
    cb.friendColor = m_Params.friendColor;
    cb.friendThickness = (std::max)(1.0f, std::round(cb.thickness * m_Params.friendThicknessMul));
    cb.enemyColor = m_Params.enemyColor;
    cb.enemyThickness = (std::max)(1.0f, std::round(cb.thickness * m_Params.enemyThicknessMul));
    m_PS->WriteBuffer(context, 0, &cb);

    context->OMSetRenderTargets(1, &rtv, nullptr);   // 深度・ステンシルを読むので DSV は外す
    m_VS->Bind(context);
    m_PS->Bind(context);
    m_PS->SetSRV(context, 0u, depth);
    m_PS->SetSRV(context, 1u, stencil);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetBlendState(m_Blend.Get(), blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(rs.DepthNone(), 0);
    context->RSSetState(rs.CullNone());
    context->Draw(3, 0);

    ID3D11ShaderResourceView* none[2] = { nullptr, nullptr };
    context->PSSetShaderResources(0, 2, none);   // 次に DSV として使う前に外す
    rs.Restore(context);
    graphics.RestoreRenderTarget();
}

void Outline::DrawImGui()
{
    ImGui::Checkbox("Outline", &m_Params.enabled);
    ImGui::DragFloat("Outline Thickness (px@1080)", &m_Params.thickness, 0.05f, 0.5f, 6.0f);
    ImGui::DragFloat("Outline Threshold", &m_Params.threshold, 0.001f, 0.002f, 0.5f, "%.3f");
    ImGui::DragFloatRange2("Outline Fade (m)", &m_Params.fadeStart, &m_Params.fadeEnd, 0.5f, 0.0f, 500.0f);
    ImGui::SliderFloat("Outline Strength", &m_Params.strength, 0.0f, 1.0f);
    ImGui::ColorEdit3("Outline Tint", &m_Params.tint.x);
    ImGui::Checkbox("Outline Debug (depth stripes)", &m_Params.debugView);
    ImGui::SeparatorText("Faction Outline (stencil)");
    ImGui::Checkbox("Faction Outline", &m_Params.factionEnabled);
    ImGui::ColorEdit3("Friend Color", &m_Params.friendColor.x);
    ImGui::ColorEdit3("Enemy Color", &m_Params.enemyColor.x);
    ImGui::DragFloat("Friend Thickness x", &m_Params.friendThicknessMul, 0.05f, 0.5f, 4.0f);
    ImGui::DragFloat("Enemy Thickness x", &m_Params.enemyThicknessMul, 0.05f, 0.5f, 4.0f);
    ImGui::SliderFloat("Faction Strength", &m_Params.factionStrength, 0.0f, 1.0f);
}
