// ============================================================
// SwarmSystemFire.cpp
// 燃えている草からの粒子の発射（2026-10-12）。GrassFireEmitCS を弾・範囲の発射（DispatchEmit）の直後に回す。
// 空きの予約（m_EmitBudget）と空き数（deadCount）を同じフレームで共有するので、火が多くても
// deadList を取り過ぎない（足りない時に先に削られるのは、後に回るこちらと経験値オーブの尾）
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/ComputeShader.h"

void SwarmSystem::SetGrassFire(const GrassFireEmitInput* in)
{
    m_HasGrassFire = (in != nullptr && in->fire && in->ground && in->height && in->vfx != 0);
    if (m_HasGrassFire) m_GrassFire = *in;
}

void SwarmSystem::DispatchFireEmit(float dt, float totalTime)
{
    if (!m_HasGrassFire || !m_FireEmitCS || !m_Particles) return;
    if (!m_VFX.GetRecipeSRV() || !m_VFX.GetEmitterSRV()) return;

    // b0 = GlobalCB（ParticleCommon）。弾の分とは別の seed
    struct GlobalCB { float dt; float total; uint32_t seed; int emitterCount; } gcb;
    gcb.dt = dt;
    gcb.total = totalTime;
    gcb.seed = (uint32_t)(totalTime * 1000.0f) ^ 0x7f4a7c15u;
    gcb.emitterCount = 0;

    // GrassFireEmitCS の b2 と同じ並び（48B）
    struct FireEmitCB
    {
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        DirectX::SimpleMath::Vector3 camPos; float emitRadius;
        float ignite, rateMul; uint32_t vfx; float heightOffset;
    } fcb;
    static_assert(sizeof(FireEmitCB) == 48, "FireEmitCB: HLSL 側と不一致");
    fcb.mapOrigin = m_GrassFire.mapOrigin;
    fcb.mapSize = m_GrassFire.mapSize;
    fcb.camPos = m_GrassFire.camPos;
    fcb.emitRadius = m_GrassFire.emitRadius;
    fcb.ignite = m_GrassFire.ignite;
    fcb.rateMul = m_GrassFire.rateMul;
    fcb.vfx = m_GrassFire.vfx;
    fcb.heightOffset = m_GrassFire.heightOffset;

    m_FireEmitCS->WriteBuffer(m_Context, 0, &gcb);
    m_FireEmitCS->WriteBuffer(m_Context, 2, &fcb);
    m_FireEmitCS->Bind(m_Context);
    m_FireEmitCS->SetSRV(m_Context, "fireMap", m_GrassFire.fire);
    m_FireEmitCS->SetSRV(m_Context, "groundMap", m_GrassFire.ground);
    m_FireEmitCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
    m_FireEmitCS->SetSRV(m_Context, "emitters", m_VFX.GetEmitterSRV());
    m_FireEmitCS->SetSRV(m_Context, "deadCount", m_Particles->GetDeadCountSRV());
    m_FireEmitCS->SetSRV(m_Context, "heightMap", m_GrassFire.height);
    m_FireEmitCS->SetUAV(m_Context, "particles", m_Particles->GetParticleUAV());
    m_FireEmitCS->SetUAV(m_Context, "deadList", m_Particles->GetDeadListUAV(), (UINT)-1);
    m_FireEmitCS->SetUAV(m_Context, "emitBudget", m_EmitBudgetUAV.Get());
    m_FireEmitCS->BindUAVs(m_Context);

    // 火のテクスチャの辺（GrassFireField::kSize）。8x8 のグループ
    const UINT groups = (m_GrassFire.fireSize + 7) / 8;
    m_Context->Dispatch(groups, groups, 1);

    m_FireEmitCS->UnbindSRVs(m_Context);
    m_FireEmitCS->UnbindUAVs(m_Context);
}
