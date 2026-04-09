#include "Renderer.h"
#include <iostream>

bool Renderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
        return false;

    m_Device = device;
    m_Context = context;

    if (!m_DefaultShader.Load(device, L"Shader/VS.hlsl", L"Shader/PS.hlsl"))
    {
        std::cout << "[Error] Default shader load failed" << std::endl;
        return false;
    }

    if (!m_MVPBuffer.Create(device))
    {
        std::cout << "[Error] MVP buffer creation failed" << std::endl;
        return false;
    }

    if (!m_LightBuffer.Create(device))
    {
        std::cout << "[Error] Light buffer creation failed" << std::endl;
        return false;
    }

    // デフォルト光源
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
{
}

void Renderer::DrawMesh(Mesh* mesh, Transform* transform, Material* material)
{
    if (!mesh || !transform || !m_Camera)
        return;

    if (material && material->GetShader())
    {
        material->Bind(m_Context);
    }
    else
    {
        m_DefaultShader.Bind(m_Context);
    }

    MVPBuffer mvp;
    mvp.World = transform->GetWorldMatrix();
    mvp.View = m_Camera->GetViewMatrix();
    mvp.Projection = m_Camera->GetProjectionMatrix();

    m_MVPBuffer.Update(m_Context, mvp);
    m_MVPBuffer.BindVS(m_Context, 0);

    m_LightData.cameraPosition = m_Camera->GetPosition();
    m_LightBuffer.Update(m_Context, m_LightData);
    m_LightBuffer.BindPS(m_Context, 0);

    mesh->Draw(m_Context);
}

void Renderer::End()
{
}