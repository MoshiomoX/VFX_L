#include "Graphics/Renderer/Skybox.h"
#include "Graphics/Renderer/Renderer.h"
#include "Camera/CameraBase.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include <iostream>

bool Skybox::Init(ID3D11Device* device,
    const std::string& modelPath,
    const std::wstring& texturePath)
{
    // 球モデル
    m_SphereModel = ResourceManager::Get().LoadModel(modelPath);
    if (!m_SphereModel)
    {
        std::cout << "[Skybox] model load failed\n";
        return false;
    }

    // パノラマテクスチャ
    m_Texture = ResourceManager::Get().LoadTexture(texturePath);
    if (!m_Texture)
    {
        std::cout << "[Skybox] texture load failed\n";
        return false;
    }

    // 専用シェーダー
    m_VS = ResourceManager::Get().LoadVS(L"SkyVS", L"Shader/SkyVS.hlsl");
    m_PS = ResourceManager::Get().LoadPS(L"SkyPS", L"Shader/SkyPS.hlsl");
    if (!m_VS || !m_PS)
    {
        std::cout << "[Skybox] shader load failed\n";
        return false;
    }

    //// --- 前面カリング（球の内側を描画する）---
    //D3D11_RASTERIZER_DESC rd = {};
    //rd.FillMode = D3D11_FILL_SOLID;
    //rd.CullMode = D3D11_CULL_FRONT;   // 内面を描く（ダメなら BACK に変える）
    //rd.FrontCounterClockwise = FALSE;
    //rd.DepthClipEnable = TRUE;
    //device->CreateRasterizerState(&rd, &m_FrontCullRS);

    //// --- 深度：書き込みあり、LESS_EQUAL ---
    //D3D11_DEPTH_STENCIL_DESC dsd = {};
    //dsd.DepthEnable = TRUE;
    //dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    //dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    //device->CreateDepthStencilState(&dsd, &m_DepthState);

    //// --- サンプラー（パノラマは横方向ループ）---
    //D3D11_SAMPLER_DESC sd = {};
    //sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    //sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    //sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    //sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    //device->CreateSamplerState(&sd, &m_Sampler);

    m_Transform.SetScale({ m_Scale, m_Scale, m_Scale });

    std::cout << "[Skybox] initialized\n";
    return true;
}

void Skybox::Render(Renderer& renderer, CameraBase* camera)
{
    if (!m_SphereModel || !camera) return;

    auto* context = renderer.GetContext();

    // 球をカメラ位置に配置（追従 → 無限遠の錯覚）
    m_Transform.SetPosition(camera->GetPosition());

    // --- 専用ステートを設定 ---
    //context->RSSetState(m_FrontCullRS.Get());
    //context->OMSetDepthStencilState(m_DepthState.Get(), 0);
    //context->PSSetSamplers(0, 1, m_Sampler.GetAddressOf());
    RenderStates::Get().ApplySkybox(context);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    context->PSSetSamplers(0, 1, &samp);
    // --- シェーダー + テクスチャ ---
    m_VS->Bind(context);
    m_PS->Bind(context);
    m_PS->SetTexture(context, 0, m_Texture.get());

    // --- MVP ---
    MVPBuffer mvp;
    mvp.World = m_Transform.GetWorldMatrix();
    mvp.View = camera->GetViewMatrix();
    mvp.Projection = camera->GetProjectionMatrix();
    m_VS->WriteBuffer(context, 0, &mvp);

    // --- 球の全 SubMesh をジオメトリだけ描画 ---
    for (auto& sub : m_SphereModel->GetSubMeshes())
    {
        if (sub.mesh)
            sub.mesh->Draw(context);
    }

    // --- ステートを戻す（後続の描画のため）---
    context->RSSetState(nullptr);
    context->OMSetDepthStencilState(nullptr, 0);
}