// ============================================================
// SpriteRenderer.cpp
// ============================================================
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Renderer/RenderStates.h"
#include <iostream>

using namespace DirectX::SimpleMath;

bool SpriteRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
    UINT maxSprites)
{
    m_Device = device;
    m_Context = context;
    m_MaxSprites = maxSprites;

    // ---- インスタンスバッファ ----
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(SpriteInstance) * maxSprites;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(SpriteInstance);

    if (FAILED(device->CreateBuffer(&bd, nullptr, &m_InstanceBuffer)))
    {
        std::cout << "[Error] SpriteRenderer: instance buffer failed" << std::endl;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = maxSprites;
    if (FAILED(device->CreateShaderResourceView(m_InstanceBuffer.Get(), &sd, &m_InstanceSRV)))
    {
        std::cout << "[Error] SpriteRenderer: SRV failed" << std::endl;
        return false;
    }

    // ---- シェーダー ----
    m_VS = ResourceManager::Get().LoadVS(L"SpriteVS", L"Shader/UI/SpriteVS.hlsl");
    m_PS = ResourceManager::Get().LoadPS(L"SpritePS", L"Shader/UI/SpritePS.hlsl");
    if (!m_VS || !m_PS)
    {
        std::cout << "[Error] SpriteRenderer: shader load failed" << std::endl;
        return false;
    }

    m_Batch.reserve(maxSprites);
    std::cout << "[OK] SpriteRenderer initialized" << std::endl;
    return true;
}

void SpriteRenderer::Shutdown()
{
    m_Batch.clear();
    m_CurrentTexture.reset();
    m_VS.reset();
    m_PS.reset();
}

void SpriteRenderer::SetScreenSize(float width, float height)
{
    m_ScreenSize = { width, height };
}

void SpriteRenderer::Begin()
{
    m_Batch.clear();
    m_CurrentTexture.reset();
    m_LastDrawCalls = 0;
    m_LastSpriteCount = 0;
    m_InBegin = true;
}

void SpriteRenderer::End()
{
    Flush();
    m_InBegin = false;

    // 次のパスへステートを持ち越さない
    RenderStates::Get().Restore(m_Context);
}

// ============================================================
// 1枚積む。テクスチャが変わる or 上限に達したら自動で Flush。
// ============================================================
void SpriteRenderer::Draw(std::shared_ptr<Texture> tex,
    const Vector2& position, const Vector2& size,
    const Vector4& color, const Vector4& uvRect)
{
    if (!m_InBegin || !tex) return;

    // テクスチャが切り替わったら、ここまでを描画してから続ける
    if (m_CurrentTexture && m_CurrentTexture != tex)
        Flush();

    if (m_Batch.size() >= m_MaxSprites)
        Flush();

    m_CurrentTexture = tex;

    SpriteInstance inst;
    inst.position = position;
    inst.size = size;
    inst.color = color;
    inst.uvRect = uvRect;
    m_Batch.push_back(inst);
}

// ============================================================
// 溜めた分を 1 回の DrawInstanced で描く
// ============================================================
void SpriteRenderer::Flush()
{
    if (m_Batch.empty() || !m_CurrentTexture) return;
    if (!m_Context || !m_VS || !m_PS) return;

    // ---- アップロード ----
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_Context->Map(m_InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        m_Batch.clear();
        return;
    }
    memcpy(mapped.pData, m_Batch.data(), sizeof(SpriteInstance) * m_Batch.size());
    m_Context->Unmap(m_InstanceBuffer.Get(), 0);

    // ---- ステート（アルファ合成、深度なし、カリングなし）----
    auto& states = RenderStates::Get();
    states.ApplyAlphaBlend(m_Context);

    ID3D11SamplerState* samp = states.LinearClamp();
    m_Context->PSSetSamplers(0, 1, &samp);

    // ---- 頂点バッファ無し（VS が SV_VertexID で quad を展開）----
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    m_Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ---- シェーダー + データ ----
    m_VS->Bind(m_Context);
    m_PS->Bind(m_Context);

    SpriteCB cb;
    cb.screenSize = m_ScreenSize;
    cb.pad0 = { 0.0f, 0.0f };
    m_VS->WriteBuffer(m_Context, 0, &cb);

    m_VS->SetSRV(m_Context, "instances", m_InstanceSRV.Get());
    m_PS->SetTexture(m_Context, 0, m_CurrentTexture.get());

    // ---- 一括描画 ----
    m_Context->DrawInstanced(6, (UINT)m_Batch.size(), 0, 0);

    m_LastSpriteCount += (UINT)m_Batch.size();
    ++m_LastDrawCalls;

    m_VS->UnbindSRVs(m_Context);
    m_Batch.clear();
}