// ============================================================
// ProjectileBillboardRenderer.cpp
// ============================================================
#include "ProjectileBillboardRenderer.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "ProjectileComponent.h"
#include "ProjectileVisualComponent.h"
#include "View.h"
#include "CameraBase.h"
#include "Texture.h"
#include "VertexShader.h"
#include "PixelShader.h"
#include "ResourceManager.h"
#include "RenderStates.h"
#include <iostream>

using namespace DirectX::SimpleMath;

bool ProjectileBillboardRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
    UINT maxProjectiles)
{
    m_Device = device;
    m_Context = context;
    m_MaxProjectiles = maxProjectiles;

    // ---- インスタンスバッファ（StructuredBuffer、毎フレーム Map で更新）----
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(ProjectileInstance) * maxProjectiles;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(ProjectileInstance);

    if (FAILED(device->CreateBuffer(&bd, nullptr, &m_InstanceBuffer)))
    {
        std::cout << "[Error] ProjectileBillboard: instance buffer failed" << std::endl;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = maxProjectiles;
    if (FAILED(device->CreateShaderResourceView(m_InstanceBuffer.Get(), &sd, &m_InstanceSRV)))
    {
        std::cout << "[Error] ProjectileBillboard: SRV failed" << std::endl;
        return false;
    }

    // ---- シェーダー（描画ステートは RenderStates 側で一元管理）----
    m_VS = ResourceManager::Get().LoadVS(L"ProjectileVS", L"Shader/Projectile/ProjectileVS.hlsl");
    m_PS = ResourceManager::Get().LoadPS(L"ProjectilePS", L"Shader/Projectile/ProjectilePS.hlsl");
    if (!m_VS || !m_PS)
    {
        std::cout << "[Error] ProjectileBillboard: shader load failed" << std::endl;
        return false;
    }

    m_Instances.reserve(maxProjectiles);
    std::cout << "[OK] ProjectileBillboardRenderer initialized" << std::endl;
    return true;
}

void ProjectileBillboardRenderer::Shutdown()
{
    m_Instances.clear();
    m_VS.reset();
    m_PS.reset();
    m_Texture.reset();
}

// ============================================================
// 描画
// ============================================================
void ProjectileBillboardRenderer::Render(Registry& reg, CameraBase* camera)
{
    if (!m_Context || !camera || !m_VS || !m_PS) return;

    // ---- 1) 投射物を収集（TransformComponent が唯一の位置真値）---
    m_Instances.clear();
    reg.CreateView<TransformComponent, ProjectileVisualComponent>()
        .Each([&](Entity e, TransformComponent& tf, ProjectileVisualComponent& vis)
            {
                if (m_Instances.size() >= m_MaxProjectiles) return;

                ProjectileInstance inst;
                inst.position = tf.position;
                inst.size = vis.size;
                inst.color = vis.color;
                inst.stretch = vis.stretch;

                if (reg.Has<ProjectileComponent>(e))
                    inst.velocity = reg.Get<ProjectileComponent>(e).velocity;
                else
                    inst.velocity = { 0.0f, 0.0f, 0.0f };

                m_Instances.push_back(inst);
            });

    m_LastDrawCount = (UINT)m_Instances.size();
    if (m_Instances.empty()) return;

    // ---- 2) アップロード（1000発でも 48KB 程度）----
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_Context->Map(m_InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, m_Instances.data(), sizeof(ProjectileInstance) * m_Instances.size());
    m_Context->Unmap(m_InstanceBuffer.Get(), 0);

    // ---- 3) 描画ステート（加算 + 深度読むだけ + カリング無し）----
    auto& states = RenderStates::Get();
    states.ApplyAdditiveBillboard(m_Context);

    ID3D11SamplerState* samp = states.LinearClamp();   // 板の縁を繰り返さない
    m_Context->PSSetSamplers(0, 1, &samp);

    // ---- 4) 頂点バッファ無し（VS が SV_VertexID で quad を展開する）----
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    m_Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ---- 5) シェーダー + データ ----
    m_VS->Bind(m_Context);
    m_PS->Bind(m_Context);

    RenderCB cb;
    // ※GPUParticleSystem と同じく、行列は Transpose して渡す
    cb.view = camera->GetViewMatrix().Transpose();
    cb.projection = camera->GetProjectionMatrix().Transpose();
    cb.cameraPosition = camera->GetPosition();
    cb.pad0 = 0.0f;
    m_VS->WriteBuffer(m_Context, 0, &cb);

    m_VS->SetSRV(m_Context, "instances", m_InstanceSRV.Get());

    if (m_Texture)
        m_PS->SetTexture(m_Context, 0, m_Texture.get());

    // ---- 6) 一括描画（quad 6頂点 × インスタンス数）----
    m_Context->DrawInstanced(6, m_LastDrawCount, 0, 0);

    // ---- 7) 後片付け（次のパスへ持ち越さない）----
    m_VS->UnbindSRVs(m_Context);
    states.Restore(m_Context);
}