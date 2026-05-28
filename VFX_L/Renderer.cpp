#include "Renderer.h"
#include <iostream>

bool Renderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
        return false;

    m_Device = device;
    m_Context = context;

    m_DefaultVS = std::make_shared<VertexShader>();
    if (FAILED(m_DefaultVS->Compile(device, L"Shader/VS.hlsl")))
    {
        std::cout << "[Error] Default VS load failed" << std::endl;
        return false;
    }

    m_DefaultPS = std::make_shared<PixelShader>();
    if (FAILED(m_DefaultPS->Compile(device, L"Shader/PS.hlsl")))
    {
        std::cout << "[Error] Default PS load failed" << std::endl;
        return false;
    }

    m_LightData.directionalLight.direction = Vector3(0.5f, -1.0f, 0.5f);
    m_LightData.directionalLight.direction.Normalize();
    m_LightData.directionalLight.color = Vector3(1.0f, 1.0f, 1.0f);
    m_LightData.directionalLight.intensity = 1.0f;
    m_LightData.ambientColor = Vector3(0.2f, 0.2f, 0.2f);

    std::cout << "[OK] Renderer initialized" << std::endl;
    return true;
}

void Renderer::Shutdown()
{
    m_DefaultVS.reset();
    m_DefaultPS.reset();
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
    }

    // VS CBuffer (b0): MVP
    MVPBuffer mvp;
    mvp.World = transform->GetWorldMatrix();
    mvp.View = m_Camera->GetViewMatrix();
    mvp.Projection = m_Camera->GetProjectionMatrix();
    vs->WriteBuffer(m_Context, 0, &mvp);

    // PS CBuffer (b0): Light
    m_LightData.cameraPosition = m_Camera->GetPosition();
    ps->WriteBuffer(m_Context, 0, &m_LightData);

    mesh->Draw(m_Context);
}

void Renderer::End()
{}