#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Graphics/Light/PointLightManager.h"
#include <iostream>

bool Renderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
        return false;

    m_Device = device;
    m_Context = context;

    m_DefaultVS = std::make_shared<VertexShader>();
    // ?Debug/Release ?? cso ???(ShaderPath ??)
    HRESULT hrVS = ShaderPath::Load(m_DefaultVS.get(), device, L"Shader/VS.hlsl");
    if (FAILED(hrVS))
    {
        std::cout << "[Error] Default VS load failed" << std::endl;
        return false;
    }
    m_DefaultPS = std::make_shared<PixelShader>();
    HRESULT hrPS = ShaderPath::Load(m_DefaultPS.get(), device, L"Shader/PS.hlsl");
    if (FAILED(hrPS))
    {
        std::cout << "[Error] Default PS load failed" << std::endl;
        return false;
    }

    m_DefaultTexture = std::make_shared<Texture>();
    m_DefaultTexture->CreateSolid(device, 255, 255, 255, 255);   // ?1x1
    m_LightData.directionalLight.direction = Vector3(0.5f, -1.0f, 0.5f);
    m_LightData.directionalLight.direction.Normalize();
    m_LightData.directionalLight.color = Vector3(1.0f, 1.0f, 1.0f);
    m_LightData.directionalLight.intensity = 1.0f;
    m_LightData.ambientColor = Vector3(0.2f, 0.2f, 0.2f);

    std::cout << "[OK] Renderer initialized" << std::endl;
    // ????????(???? + WRAP)?PBR?t0~t4???
    //D3D11_SAMPLER_DESC sd = {};
    //sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    //sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    //sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    //sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    //sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    //sd.MinLOD = 0;
    //sd.MaxLOD = D3D11_FLOAT32_MAX;

    //if (FAILED(device->CreateSamplerState(&sd, &m_DefaultSampler)))
    //{
    //    std::cout << "[Error] Default sampler create failed" << std::endl;
    //    return false;
    //}
    if (!RenderStates::Get().Initialize(device))
    {
        std::cout << "[Error] RenderStates init failed" << std::endl;
        return false;
    }
    if (!PointLightManager::Get().Initialize(device))
    {
        std::cout << "[Error] PointLightManager init failed" << std::endl;
        return false;
    }
    return true;
}

void Renderer::Shutdown()
{
    m_DefaultVS.reset();
    m_DefaultPS.reset();
    PointLightManager::Get().Shutdown();
    RenderStates::Get().Shutdown();
    m_Device = nullptr;
    m_Context = nullptr;
    m_Camera = nullptr;
}

void Renderer::SetDirectionalLight(const Vector3& direction, const Vector3& color, float intensity)
{
    m_LightData.directionalLight.direction = direction;
    m_LightData.directionalLight.direction.Normalize(); 
    m_LightData.directionalLight.color = color;
    m_LightData.directionalLight.intensity = intensity;
}

void Renderer::SetAmbientColor(const Vector3& color)
{
    m_LightData.ambientColor = color;
}

void Renderer::Begin()
{}

void Renderer::DrawMesh(Mesh* mesh, Transform* transform, Material* material)
{
    if (!mesh || !transform || !m_Camera)
        return;

    VertexShader* vs = nullptr;
    PixelShader* ps = nullptr;

    if (material && material->HasVS())
        vs = material->GetVS();
    else
        vs = m_DefaultVS.get();

    if (material && material->HasPS())
        ps = material->GetPS();
    else
        ps = m_DefaultPS.get();

    if (material)
    {
        material->Bind(m_Context);
    }
    else
    {
        vs->Bind(m_Context);
        ps->Bind(m_Context);
        // ??????? ? ??????? t0 ?(?????????)
        if (m_DefaultTexture)
            m_DefaultTexture->Bind(m_Context, 0);
    }
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    m_Context->PSSetSamplers(0, 1, &samp);

    // VS CBuffer (b0): MVP
    MVPBuffer mvp;
    mvp.World = transform->GetWorldMatrix();
    mvp.View = m_Camera->GetViewMatrix();
    mvp.Projection = m_Camera->GetProjectionMatrix();
    vs->WriteBuffer(m_Context, 0, &mvp);

    // PS CBuffer (b0): Light
    m_LightData.cameraPosition = m_Camera->GetPosition();
    ps->WriteBuffer(m_Context, 0, &m_LightData);

    // PS CBuffer (b1) + t5: 溶解。無い時も threshold = -1 を書いて前の値を残さない
    // （b1 を持たない PS では WriteBuffer が何もしない）
    DissolveCB dcb = {};
    dcb.threshold = -1.0f;
    ID3D11ShaderResourceView* noise = nullptr;
    if (m_Dissolve && m_Dissolve->noise && m_Dissolve->threshold >= 0.0f)
    {
        dcb.tiling = m_Dissolve->tiling;
        dcb.scroll = m_Dissolve->scroll;
        dcb.threshold = m_Dissolve->threshold;
        dcb.edge = m_Dissolve->edge;
        dcb.edgeColor = m_Dissolve->edgeColor;
        noise = m_Dissolve->noise;
    }
    ps->WriteBuffer(m_Context, 1, &dcb);
    m_Context->PSSetShaderResources(5, 1, &noise);

    // t6/t7: 今フレームの点光源（無い PS では無視される）
    auto& lights = PointLightManager::Get();
    lights.BindPS(m_Context);
    mesh->Draw(m_Context);
    // 次フレームの Update で CS が UAV にするので外す（HAZARD 警告を出さない）
    lights.UnbindPS(m_Context);
}

void Renderer::End()
{}