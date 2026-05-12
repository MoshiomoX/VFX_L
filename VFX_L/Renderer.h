#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "ConstantBuffer.h"
#include "CameraBase.h"
#include "Mesh.h"
#include "Transform.h"
#include "Material.h"
#include "Shader.h"
#include "LightTypes.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX::SimpleMath;

struct MVPBuffer
{
    Matrix World;
    Matrix View;
    Matrix Projection;
};

class Renderer
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    void SetCamera(CameraBase* camera) { m_Camera = camera; }

    void SetDirectionalLight(const Vector3& direction, const Vector3& color, float intensity);
    void SetAmbientColor(const Vector3& color);

    void Begin();
    void DrawMesh(Mesh* mesh, Transform* transform, Material* material = nullptr);
    void End();

    ID3D11Device* GetDevice() const { return m_Device; }
    ID3D11DeviceContext* GetContext() const { return m_Context; }

private:
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    Shader m_DefaultShader;
    ConstantBuffer<MVPBuffer> m_MVPBuffer;
    ConstantBuffer<LightBuffer> m_LightBuffer;
    CameraBase* m_Camera = nullptr;

    LightBuffer m_LightData;
};