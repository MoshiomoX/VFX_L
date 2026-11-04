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

void SwarmSystem::SetGrassFireDamage(ID3D11ShaderResourceView* fireMap, const DirectX::SimpleMath::Vector2& origin,
    const DirectX::SimpleMath::Vector2& size, float damage, float tick)
{
    m_FireMapSRV = (damage > 0.0f) ? fireMap : nullptr;
    auto& b = GetBomberParams();
    b.fireOn = m_FireMapSRV ? 1.0f : 0.0f;
    b.fireDamage = damage;
    b.fireTick = tick;
    b.fireOriginX = origin.x;
    b.fireOriginZ = origin.y;
    b.fireSizeX = size.x;
    b.fireSizeZ = size.y;
}

void SwarmSystem::SetIceMaps(ID3D11ShaderResourceView* ice, ID3D11ShaderResourceView* pondTop)
{
    m_IceMapSRV = ice;
    m_PondTopSRV = pondTop;
    GetBomberParams().iceOn = (ice && pondTop) ? 1.0f : 0.0f;
}

void SwarmSystem::SetSandHaze(const GrassFireEmitInput* in)
{
    m_HasSandHaze = (in != nullptr && in->fire && in->ground && in->height && in->vfx != 0);
    if (m_HasSandHaze) m_SandHaze = *in;
}

void SwarmSystem::SetSandDust(const GrassFireEmitInput* in)
{
    m_HasSandDust = (in != nullptr && in->fire && in->ground && in->height && in->vfx != 0);
    if (m_HasSandDust) m_SandDust = *in;
}

// 草の火・砂の陽炎（2026-11-02）・風に舞う砂（2026-11-03）は同じ CS を入力を替えて回す
void SwarmSystem::DispatchFireEmit(float dt, float totalTime)
{
    if (m_HasGrassFire) DispatchFireEmitOne(m_GrassFire, dt, totalTime, 0x7f4a7c15u);
    if (m_HasSandHaze) DispatchFireEmitOne(m_SandHaze, dt, totalTime, 0x2c1b3c6du);
    if (m_HasSandDust) DispatchFireEmitOne(m_SandDust, dt, totalTime, 0x5be1d3a7u);
}

void SwarmSystem::DispatchFireEmitOne(const GrassFireEmitInput& in, float dt, float totalTime, uint32_t seedXor)
{
    if (!m_FireEmitCS || !m_Particles) return;
    if (!m_VFX.GetRecipeSRV() || !m_VFX.GetEmitterSRV()) return;

    // b0 = GlobalCB（ParticleCommon）。弾の分とは別の seed
    struct GlobalCB { float dt; float total; uint32_t seed; int emitterCount; } gcb;
    gcb.dt = dt;
    gcb.total = totalTime;
    gcb.seed = (uint32_t)(totalTime * 1000.0f) ^ seedXor;
    gcb.emitterCount = 0;

    // GrassFireEmitCS の b2 と同じ並び（64B）
    struct FireEmitCB
    {
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        DirectX::SimpleMath::Vector3 camPos; float emitRadius;
        float ignite, rateMul; uint32_t vfx; float heightOffset;
        uint32_t channel; float pad[3];
    } fcb = {};
    static_assert(sizeof(FireEmitCB) == 64, "FireEmitCB: HLSL 側と不一致");
    fcb.mapOrigin = in.mapOrigin;
    fcb.mapSize = in.mapSize;
    fcb.camPos = in.camPos;
    fcb.emitRadius = in.emitRadius;
    fcb.ignite = in.ignite;
    fcb.rateMul = in.rateMul;
    fcb.vfx = in.vfx;
    fcb.heightOffset = in.heightOffset;
    fcb.channel = in.channel;

    m_FireEmitCS->WriteBuffer(m_Context, 0, &gcb);
    m_FireEmitCS->WriteBuffer(m_Context, 2, &fcb);
    m_FireEmitCS->Bind(m_Context);
    m_FireEmitCS->SetSRV(m_Context, "fireMap", in.fire);
    m_FireEmitCS->SetSRV(m_Context, "groundMap", in.ground);
    m_FireEmitCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
    m_FireEmitCS->SetSRV(m_Context, "emitters", m_VFX.GetEmitterSRV());
    m_FireEmitCS->SetSRV(m_Context, "deadCount", m_Particles->GetDeadCountSRV());
    m_FireEmitCS->SetSRV(m_Context, "heightMap", in.height);
    m_FireEmitCS->SetUAV(m_Context, "particles", m_Particles->GetParticleUAV());
    m_FireEmitCS->SetUAV(m_Context, "deadList", m_Particles->GetDeadListUAV(), (UINT)-1);
    m_FireEmitCS->SetUAV(m_Context, "emitBudget", m_EmitBudgetUAV.Get());
    m_FireEmitCS->BindUAVs(m_Context);

    // 火のテクスチャの辺（GrassFireField::kSize）。8x8 のグループ
    const UINT groups = (in.fireSize + 7) / 8;
    m_Context->Dispatch(groups, groups, 1);

    m_FireEmitCS->UnbindSRVs(m_Context);
    m_FireEmitCS->UnbindUAVs(m_Context);
}
