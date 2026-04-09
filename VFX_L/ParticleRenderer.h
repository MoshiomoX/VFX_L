#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include "Particle.h"
#include "Shader.h"
#include "Texture.h"
#include "Camera.h"
#include "ConstantBuffer.h"

using Microsoft::WRL::ComPtr;

// 粒子用頂点
struct ParticleVertex
{
    Vector3 position;
    Vector2 uv;
    Vector4 color;
    float size;
};

// 定数バッファ
struct ParticleCB
{
    Matrix View;
    Matrix Projection;
    Vector3 CameraRight;
    float padding1;
    Vector3 CameraUp;
    float padding2;
};

class ParticleRenderer
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, int maxParticles = 1000);
    void Shutdown();

    void SetCamera(Camera* camera) { m_Camera = camera; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }

    void Render(const std::vector<Particle>& particles);

private:
    bool CreateBuffers(int maxParticles);
    bool CreateShaders();
    bool CreateBlendState();
    void UpdateVertexBuffer(const std::vector<Particle>& particles);

private:
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    ComPtr<ID3D11Buffer> m_VertexBuffer;
    ComPtr<ID3D11Buffer> m_IndexBuffer;
    ConstantBuffer<ParticleCB> m_ConstantBuffer;

    Shader m_Shader;
    std::shared_ptr<Texture> m_Texture;
    Camera* m_Camera = nullptr;

    ComPtr<ID3D11BlendState> m_BlendState;
    ComPtr<ID3D11DepthStencilState> m_DepthState;

    int m_MaxParticles = 0;
    int m_CurrentParticleCount = 0;
};