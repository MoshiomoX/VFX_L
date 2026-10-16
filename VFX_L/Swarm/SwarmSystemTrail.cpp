// ============================================================
// SwarmSystemTrail.cpp
// GPU の弾が引く「切れない帯」（2026-10-15、ホーミングボルト）。
//   VFX の Trail entry をレシピに写し（SwarmVFXTable::recipeTrail）、弾の槽ごとに位置の環を持つ。
//   記録は固定ステップごと（SwarmProjTrailCS、弾の積分の直後）、描画は帯 1 本 = 弾の槽 1 つ
//   （SwarmProjTrailVS + 粒子の帯と同じ ParticleTrailPS。style ごとに 1 回 DrawInstanced）。
//   弾が消えた槽は CS が環を空にする（帯は命中の瞬間に消える。消える所は命中の火花が隠す）
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Particle/GPUParticle.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Material/Texture.h"
#include "Camera/CameraBase.h"

#include <chrono>

void SwarmSystem::DispatchProjTrails()
{
    if (!m_ProjTrailCS || m_VFX.GetTrailStyleCount() == 0) return;
    if (!m_VFX.GetRecipeTrailSRV() || !m_VFX.GetTrailStyleSRV()) return;

    m_ProjTrailCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
    m_ProjTrailCS->Bind(m_Context);
    m_ProjTrailCS->SetSRV(m_Context, "recipeTrail", m_VFX.GetRecipeTrailSRV());
    m_ProjTrailCS->SetSRV(m_Context, "trailStyles", m_VFX.GetTrailStyleSRV());
    m_ProjTrailCS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
    m_ProjTrailCS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());
    m_ProjTrailCS->SetUAV(m_Context, "trailPoints", m_ProjTrailPointsUAV.Get());
    m_ProjTrailCS->SetUAV(m_Context, "trailState", m_ProjTrailStateUAV.Get());
    m_ProjTrailCS->SetUAV(m_Context, "trailAge", m_ProjTrailAgeUAV.Get());
    m_ProjTrailCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxProjectiles + 255) / 256, 1, 1);
    m_ProjTrailCS->UnbindSRVs(m_Context);
    m_ProjTrailCS->UnbindUAVs(m_Context);
}

void SwarmSystem::RenderProjTrails(CameraBase* camera)
{
    if (!camera || !m_ProjTrailVS || !m_ProjTrailPS || m_VFX.GetTrailStyleCount() == 0) return;
    if (!m_ProjTrailPointsSRV || !m_VFX.GetRecipeTrailSRV()) return;

    if (!m_TrailWhite)
    {
        m_TrailWhite = std::make_shared<Texture>();
        m_TrailWhite->CreateSolid(m_Device, 255, 255, 255, 255);
    }

    ParticleRenderCB rcb = {};
    rcb.view = camera->GetViewMatrix().Transpose();
    rcb.projection = camera->GetProjectionMatrix().Transpose();
    rcb.cameraPosition = camera->GetPosition();
    m_ProjTrailVS->WriteBuffer(m_Context, 0, &rcb);

    m_ProjTrailVS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
    m_ProjTrailVS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());
    m_ProjTrailVS->SetSRV(m_Context, "recipeTrail", m_VFX.GetRecipeTrailSRV());
    m_ProjTrailVS->SetSRV(m_Context, "trailStyles", m_VFX.GetTrailStyleSRV());
    m_ProjTrailVS->SetSRV(m_Context, "trailPoints", m_ProjTrailPointsSRV.Get());
    m_ProjTrailVS->SetSRV(m_Context, "trailState", m_ProjTrailStateSRV.Get());
    m_ProjTrailVS->SetSRV(m_Context, "trailAge", m_ProjTrailAgeSRV.Get());

    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();   // 長さ方向（U）は繰り返す
    m_Context->PSSetSamplers(0, 1, &samp);

    m_ProjTrailVS->Bind(m_Context);
    m_ProjTrailPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    // uvScroll 用の時計（秒）。描画だけに使うので実時間で十分
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point s_Start = Clock::now();
    const float now = std::chrono::duration<float>(Clock::now() - s_Start).count();

    for (int i = 0; i < m_VFX.GetTrailStyleCount(); ++i)
    {
        const auto& look = m_VFX.GetTrailLook(i);
        TrailDrawCB cb = {};
        cb.styleSlot = (uint32_t)(i + 1);
        cb.premultiply = look.alpha ? 1u : 0u;
        cb.time = now;
        m_ProjTrailVS->WriteBuffer(m_Context, 1, &cb);
        m_ProjTrailPS->WriteBuffer(m_Context, 1, &cb);
        m_ProjTrailPS->SetTexture(m_Context, 0, look.texture ? look.texture.get() : m_TrailWhite.get());

        if (look.alpha) RenderStates::Get().ApplyAlphaBlend(m_Context);
        else            RenderStates::Get().ApplyAdditiveBillboard(m_Context);

        m_Context->DrawInstanced(2u * (kTrailPoints + 1u), Swarm::kMaxProjectiles, 0, 0);
    }

    RenderStates::Get().Restore(m_Context);
    // 次のフレームで CS が UAV として使うので必ず外す
    m_ProjTrailVS->UnbindSRVs(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
