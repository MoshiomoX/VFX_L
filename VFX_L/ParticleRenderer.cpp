#include "ParticleRenderer.h"
#include <iostream>

bool ParticleRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, int maxParticles)
{
    m_Device = device;
    m_Context = context;
    m_MaxParticles = maxParticles;

    if (!CreateBuffers(maxParticles))
        return false;

    if (!CreateShaders())
        return false;

    if (!CreateBlendState())
        return false;

    if (!m_ConstantBuffer.Create(device))
        return false;

    std::cout << "[OK] ParticleRenderer initialized" << std::endl;
    return true;
}

void ParticleRenderer::Shutdown()
{
    m_VertexBuffer.Reset();
    m_IndexBuffer.Reset();
    m_BlendState.Reset();
    m_DepthState.Reset();
}

bool ParticleRenderer::CreateBuffers(int maxParticles)
{
    // 頂点バッファ（動的）
    // 1粒子 = 4頂点（四角形）
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(ParticleVertex) * maxParticles * 4;
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_Device->CreateBuffer(&vbDesc, nullptr, &m_VertexBuffer);
    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create particle vertex buffer" << std::endl;
        return false;
    }

    // インデックスバッファ（静的）
    // 1粒子 = 6インデックス（2三角形）
    std::vector<unsigned int> indices;
    indices.reserve(maxParticles * 6);

    for (int i = 0; i < maxParticles; i++)
    {
        unsigned int base = i * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
    }

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(unsigned int) * (UINT)indices.size();
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = indices.data();

    hr = m_Device->CreateBuffer(&ibDesc, &ibData, &m_IndexBuffer);
    if (FAILED(hr))
    {
        std::cout << "[Error] Failed to create particle index buffer" << std::endl;
        return false;
    }

    return true;
}

bool ParticleRenderer::CreateShaders()
{
    // LoadParticle を使う
    if (!m_Shader.LoadParticle(m_Device, L"Shader/Particle/ParticleVS.hlsl", L"Shader/Particle/ParticlePS.hlsl"))
    {
        std::cout << "[Error] Failed to load particle shaders" << std::endl;
        return false;
    }
    return true;
}


bool ParticleRenderer::CreateBlendState()
{
    // 加算合成（火、光など）
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE; 
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = m_Device->CreateBlendState(&blendDesc, &m_BlendState);
    if (FAILED(hr))
        return false;

    // 深度書き込みOFF（読み取りのみ）
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // 書き込みOFF
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;

    hr = m_Device->CreateDepthStencilState(&dsDesc, &m_DepthState);
    if (FAILED(hr))
        return false;

    return true;
}

void ParticleRenderer::UpdateVertexBuffer(const std::vector<Particle>& particles)
{
    if (!m_Camera)
        return;

    // カメラのRight/Up取得
    Matrix viewMatrix = m_Camera->GetViewMatrix();
    Vector3 camRight(viewMatrix._11, viewMatrix._21, viewMatrix._31);
    Vector3 camUp(viewMatrix._12, viewMatrix._22, viewMatrix._32);

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_Context->Map(m_VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
        return;

    ParticleVertex* vertices = (ParticleVertex*)mapped.pData;
    int vertexIndex = 0;
    m_CurrentParticleCount = 0;

    for (const auto& p : particles)
    {
        if (!p.isAlive)
            continue;

        if (m_CurrentParticleCount >= m_MaxParticles)
            break;

        float halfSize = p.size * 0.5f;
        Vector3 right = camRight * halfSize;
        Vector3 up = camUp * halfSize;

        // 4頂点（Billboard）
        // 左上
        vertices[vertexIndex + 0].position = p.position - right + up;
        vertices[vertexIndex + 0].uv = Vector2(0, 0);
        vertices[vertexIndex + 0].color = p.color;
        vertices[vertexIndex + 0].size = p.size;

        // 右上
        vertices[vertexIndex + 1].position = p.position + right + up;
        vertices[vertexIndex + 1].uv = Vector2(1, 0);
        vertices[vertexIndex + 1].color = p.color;
        vertices[vertexIndex + 1].size = p.size;

        // 左下
        vertices[vertexIndex + 2].position = p.position - right - up;
        vertices[vertexIndex + 2].uv = Vector2(0, 1);
        vertices[vertexIndex + 2].color = p.color;
        vertices[vertexIndex + 2].size = p.size;

        // 右下
        vertices[vertexIndex + 3].position = p.position + right - up;
        vertices[vertexIndex + 3].uv = Vector2(1, 1);
        vertices[vertexIndex + 3].color = p.color;
        vertices[vertexIndex + 3].size = p.size;

        vertexIndex += 4;
        m_CurrentParticleCount++;
    }

    m_Context->Unmap(m_VertexBuffer.Get(), 0);
}

void ParticleRenderer::Render(const std::vector<Particle>& particles)
{
    if (!m_Camera || particles.empty())
        return;

    // 頂点更新
    UpdateVertexBuffer(particles);

    if (m_CurrentParticleCount == 0)
        return;

    // 定数バッファ更新
    ParticleCB cb;
    cb.View = m_Camera->GetViewMatrix();
    cb.Projection = m_Camera->GetProjectionMatrix();
    
    Matrix viewMatrix = m_Camera->GetViewMatrix();
    cb.CameraRight = Vector3(viewMatrix._11, viewMatrix._21, viewMatrix._31);
    cb.CameraUp = Vector3(viewMatrix._12, viewMatrix._22, viewMatrix._32);

    m_ConstantBuffer.Update(m_Context, cb);
    m_ConstantBuffer.BindVS(m_Context, 0);

    // 状態設定
    float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(m_BlendState.Get(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(m_DepthState.Get(), 0);

    // シェーダー
    m_Shader.Bind(m_Context);

    // テクスチャ
    if (m_Texture)
    {
        m_Texture->Bind(m_Context, 0);
    }

    // 頂点バッファ
    UINT stride = sizeof(ParticleVertex);
    UINT offset = 0;
    m_Context->IASetVertexBuffers(0, 1, m_VertexBuffer.GetAddressOf(), &stride, &offset);
    m_Context->IASetIndexBuffer(m_IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 描画
    m_Context->DrawIndexed(m_CurrentParticleCount * 6, 0, 0);

    // 状態を戻す
    m_Context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(nullptr, 0);
}