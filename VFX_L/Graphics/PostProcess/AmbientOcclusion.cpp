// ============================================================
// AmbientOcclusion.cpp
// ============================================================
#include "Graphics/PostProcess/AmbientOcclusion.h"
#include "Graphics/Graphics.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Camera/CameraBase.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

bool AmbientOcclusion::Initialize(ID3D11Device* device)
{
    m_VS = std::make_shared<VertexShader>();
    m_PS = std::make_shared<PixelShader>();
    m_BlurPS = std::make_shared<PixelShader>();
    // 全画面の三角形は空・アウトラインと同じ物
    const bool ok = SUCCEEDED(ShaderPath::Load(m_VS.get(), device, L"Shader/Sky/SkyVS.hlsl"))
        && SUCCEEDED(ShaderPath::Load(m_PS.get(), device, L"Shader/PostProcess/SSAOPS.hlsl"))
        && SUCCEEDED(ShaderPath::Load(m_BlurPS.get(), device, L"Shader/PostProcess/SSAOBlurPS.hlsl"));
    std::cout << "[SSAO] " << (ok ? "OK" : "FAILED (no ambient occlusion)") << std::endl;
    if (!ok) { m_VS.reset(); m_PS.reset(); m_BlurPS.reset(); }
    return ok;
}

// AO の大きさ（画面か、その半分）の R16G16 を 3 枚（生 + 交互の 2 枚）。大きさが変わったら作り直す
bool AmbientOcclusion::EnsureTargets(ID3D11Device* device, UINT width, UINT height)
{
    if (m_Tex[0] && m_Width == width && m_Height == height) return true;
    if (!device || width == 0 || height == 0) return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16_FLOAT;   // x = ao, y = その画素の目からの距離（陰影が再投影の当否を確かめる。2026-11-02）
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    auto make = [&](Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex, Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
        {
            tex.Reset(); rtv.Reset(); srv.Reset();
            if (FAILED(device->CreateTexture2D(&td, nullptr, &tex))) return false;
            if (FAILED(device->CreateRenderTargetView(tex.Get(), nullptr, &rtv))) return false;
            return SUCCEEDED(device->CreateShaderResourceView(tex.Get(), nullptr, &srv));
        };
    if (!make(m_RawTex, m_RawRTV, m_RawSRV)) return false;
    for (int i = 0; i < 2; ++i)
    {
        if (!make(m_Tex[i], m_RTV[i], m_SRV[i])) return false;
        m_Valid[i] = false;   // 大きさが変わった = 前の物は使えない
    }
    m_Width = width;
    m_Height = height;
    return true;
}

// ============================================================
// 描画の最初：前のフレームの結果を張る。無ければ（切っている・最初のフレーム・大きさが変わった）係数 0 = 読まない
// ============================================================
void AmbientOcclusion::BindForShading(ID3D11DeviceContext* context, Renderer& renderer)
{
    const bool use = m_Params.enabled && IsReady() && m_Valid[m_Cur] && context;
    if (use)
    {
        ID3D11ShaderResourceView* srv = m_SRV[m_Cur].Get();
        context->PSSetShaderResources(kSlot, 1, &srv);
        renderer.SetAmbientOcclusion(m_ViewProj[m_Cur],
            Vector4(1.0f, m_Params.borderShift, m_Params.ambientMul, m_Params.debugView ? 1.0f : 0.0f));
    }
    else
        Disable(renderer);
}

void AmbientOcclusion::Disable(Renderer& renderer)
{
    renderer.SetAmbientOcclusion(Matrix::Identity, Vector4::Zero);
}

void AmbientOcclusion::Unbind(ID3D11DeviceContext* context)
{
    if (!context) return;
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(kSlot, 1, &none);
}

// ============================================================
// 不透明な物の後：深度（MSAA の 0 番目）から生の AO → 深度を見るぼかし → 次のフレームの分
// 深度を SRV で読むので DSV を外し、終わったら Graphics::RestoreRenderTarget
// ============================================================
void AmbientOcclusion::Render(ID3D11DeviceContext* context, Graphics& graphics, CameraBase* camera)
{
    const int next = 1 - m_Cur;
    if (!m_Params.enabled || !IsReady() || !context || !camera || graphics.GetSampleCount() <= 1)
    {
        m_Valid[next] = false;   // 次のフレームは読まない
        m_Cur = next;
        return;
    }
    ID3D11ShaderResourceView* depth = graphics.GetDepthSRV();
    // 半分の大きさ（2026-11-05）：AO は 5x5 でぼかす低周波の量なので、半分で作っても陰影の側は
    // そのまま Load（Lighting.hlsli の SceneAO は自分の貼り絵の大きさで uv を画素にする）
    const int scale = m_Params.halfRes ? 2 : 1;
    const UINT aoW = ((UINT)graphics.GetWidth() + scale - 1) / scale;
    const UINT aoH = ((UINT)graphics.GetHeight() + scale - 1) / scale;
    if (!depth || !EnsureTargets(graphics.GetDevice(), aoW, aoH))
    {
        m_Valid[next] = false;
        m_Cur = next;
        return;
    }
    m_Scale = scale;

    // 投影行列から「深度 → 目からの距離」（アウトラインと同じ式）と、画面 → カメラ空間の倍率
    const Matrix proj = camera->GetProjectionMatrix();
    const Matrix view = camera->GetViewMatrix();
    SSAOCB cb = {};
    cb.depthA = proj._33 * proj._34;
    cb.depthB = proj._43;
    cb.proj11 = proj._11;
    cb.proj22 = proj._22;
    cb.radius = m_Params.radius;
    cb.bias = m_Params.bias;
    cb.strength = m_Params.strength;
    cb.maxDistance = m_Params.maxDistance;
    cb.invView = view.Invert();
    cb.pixelScale = (float)m_Scale;
    m_PS->WriteBuffer(context, 0, &cb);
    BlurCB bcb = {};
    bcb.depthA = cb.depthA;
    bcb.depthB = cb.depthB;
    bcb.tolerance = m_Params.blurTolerance;
    bcb.pixelScale = (float)m_Scale;
    m_BlurPS->WriteBuffer(context, 0, &bcb);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(rs.DepthNone(), 0);
    context->RSSetState(rs.CullNone());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    m_VS->Bind(context);
    // 貼り絵の大きさのビューポート（半分の時。最後の RestoreRenderTarget で画面の物に戻る）
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_Width;
    vp.Height = (float)m_Height;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    // 1) 生の AO
    ID3D11RenderTargetView* raw = m_RawRTV.Get();
    context->OMSetRenderTargets(1, &raw, nullptr);   // 深度を読むので DSV は外す
    m_PS->Bind(context);
    m_PS->SetSRV(context, 0u, depth);
    context->Draw(3, 0);

    // 2) ぼかし → 次のフレームが読む方
    ID3D11RenderTargetView* out = m_RTV[next].Get();
    context->OMSetRenderTargets(1, &out, nullptr);
    m_BlurPS->Bind(context);
    m_BlurPS->SetSRV(context, 0u, m_RawSRV.Get());
    m_BlurPS->SetSRV(context, 1u, depth);
    context->Draw(3, 0);

    ID3D11ShaderResourceView* none[2] = { nullptr, nullptr };
    context->PSSetShaderResources(0, 2, none);   // 深度を次に DSV として使う前に外す
    rs.Restore(context);
    graphics.RestoreRenderTarget();

    m_ViewProj[next] = view * proj;
    m_Valid[next] = true;
    m_Cur = next;
}

void AmbientOcclusion::DrawImGui()
{
    ImGui::Checkbox("SSAO", &m_Params.enabled);
    ImGui::DragFloat("AO Radius (m)", &m_Params.radius, 0.02f, 0.1f, 4.0f);
    ImGui::DragFloat("AO Bias (m)", &m_Params.bias, 0.002f, 0.0f, 0.3f, "%.3f");
    ImGui::SliderFloat("AO Strength", &m_Params.strength, 0.0f, 2.5f);
    ImGui::DragFloat("AO Max Distance (m)", &m_Params.maxDistance, 0.5f, 5.0f, 300.0f);
    ImGui::SliderFloat("AO Blur Depth Tolerance", &m_Params.blurTolerance, 0.005f, 0.3f, "%.3f");
    ImGui::Checkbox("Half resolution", &m_Params.halfRes);
    ImGui::SameLine();
    ImGui::TextDisabled("(%u x %u)", m_Width, m_Height);
    ImGui::SeparatorText("How the shading uses it");
    ImGui::SliderFloat("Toon Border Shift (N.L)", &m_Params.borderShift, 0.0f, 1.0f);
    ImGui::SliderFloat("Ambient Multiply", &m_Params.ambientMul, 0.0f, 1.0f);
    ImGui::Checkbox("AO Debug View (grey)", &m_Params.debugView);
    ImGui::TextDisabled("the shading reads last frame's AO (static world reprojects exactly)");
}
