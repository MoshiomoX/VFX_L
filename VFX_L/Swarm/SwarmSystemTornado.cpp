// ============================================================
// SwarmSystemTornado.cpp
// 砂嵐の術師の砂旋（kEnemyKindTornado）の見た目。10-19 に筒のモデル（CreateTornadoShell + 流線のテクスチャ）で作り直したが、
// 10-20 ユーザー「線の感じが過ぎる、筒は要らない。空間の歪みに薄い白を足せば足りる」→ 筒は廃止。砂旋そのものは何も描かない。
// 見えるのは ① 足元の砂埃（SwarmTornadoEmitCS が TornadoDust.json を毎フレーム出す：屈折 + 薄い白の煙団が軸の周りを回り上がる、
// 少量の砂粒）② 風源の表（SwarmWindSourceCS）で草 / 落ち葉 / 粒子が巻かれる所。シェーダは SwarmSystemKinds.cpp の LoadKindAssets
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Particle/GPUParticleSystem.h"

using namespace DirectX::SimpleMath;

// ============================================================
// 足元の砂埃（Flush の中、草の火の発射の直後。空きの予約 m_EmitBudget を共有）
// ============================================================
void SwarmSystem::DispatchTornadoEmit(float dt, float totalTime)
{
    if (!m_TornadoEmitCS || !m_Particles || m_TornadoDustVfx == 0 || !tornadoLook.dust) return;
    if (!m_VFX.GetRecipeSRV() || !m_VFX.GetEmitterSRV()) return;

    struct GlobalCB { float dt; float total; uint32_t seed; int emitterCount; } gcb;
    gcb.dt = dt;
    gcb.total = totalTime;
    gcb.seed = (uint32_t)(totalTime * 1000.0f) ^ 0x3c6ef372u;
    gcb.emitterCount = 0;

    // SwarmTornadoEmitCS の b2 と同じ並び（32B）
    struct TornadoEmitCB
    {
        float footY, rateMul; uint32_t vfx; float swirl;
        float footRadius; uint32_t slotCount; float pad[2];
    } tcb;
    static_assert(sizeof(TornadoEmitCB) == 32, "TornadoEmitCB: HLSL 側と不一致");
    tcb.footY = -(m_CachedAICB.enemyRadius + m_CachedAICB.enemyCapsuleHalf);
    tcb.rateMul = tornadoLook.dustRate;
    tcb.vfx = m_TornadoDustVfx;
    tcb.swirl = tornadoLook.dustSwirl;
    tcb.footRadius = m_CachedAICB.enemyRadius * m_CachedBomberCB.tornadoScale;
    tcb.slotCount = Swarm::kMaxEnemies;
    tcb.pad[0] = tcb.pad[1] = 0.0f;

    m_TornadoEmitCS->WriteBuffer(m_Context, 0, &gcb);
    m_TornadoEmitCS->WriteBuffer(m_Context, 2, &tcb);
    m_TornadoEmitCS->Bind(m_Context);
    m_TornadoEmitCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_TornadoEmitCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_TornadoEmitCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_TornadoEmitCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
    m_TornadoEmitCS->SetSRV(m_Context, "emitters", m_VFX.GetEmitterSRV());
    m_TornadoEmitCS->SetSRV(m_Context, "deadCount", m_Particles->GetDeadCountSRV());
    m_TornadoEmitCS->SetUAV(m_Context, "particles", m_Particles->GetParticleUAV());
    m_TornadoEmitCS->SetUAV(m_Context, "deadList", m_Particles->GetDeadListUAV(), (UINT)-1);
    m_TornadoEmitCS->SetUAV(m_Context, "emitBudget", m_EmitBudgetUAV.Get());
    m_TornadoEmitCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_TornadoEmitCS->UnbindSRVs(m_Context);
    m_TornadoEmitCS->UnbindUAVs(m_Context);
}

// ============================================================
// 局所の風源の表（Flush の中、砂埃の直後）
// 表と枠取りの数を 0 に戻してから、生きている砂旋 1 本 = 1 つ書く（SwarmWindSourceCS）。
// 読む側（GrassVS / DebrisUpdateCS / ParticleUpdateCS）は radius 0 の枠を飛ばす
// ============================================================
void SwarmSystem::DispatchWindSources(float dt)
{
    if (!m_WindSourceUAV || !m_WindSourceCountUAV) return;
    const UINT zeros[4] = { 0, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_WindSourceUAV.Get(), zeros);
    m_Context->ClearUnorderedAccessViewUint(m_WindSourceCountUAV.Get(), zeros);
    if (!m_WindSourceCS) return;

    // ---- CPU の風源（突風。2026-10-20）：歳を進め、生きている物を表の形にして上げる ----
    uint32_t cpuCount = 0;
    if (m_WindBurstBuffer)
    {
        Swarm::WindSource cpu[Swarm::kMaxWindSources] = {};
        for (size_t i = 0; i < m_WindBursts.size();)
        {
            WindBurst& b = m_WindBursts[i];
            b.age += dt;
            if (b.age >= b.life) { m_WindBursts.erase(m_WindBursts.begin() + i); continue; }
            if (cpuCount < Swarm::kMaxWindSources)
            {
                Swarm::WindSource& w = cpu[cpuCount++];
                w.pos[0] = b.pos.x; w.pos[1] = b.pos.y; w.pos[2] = b.pos.z;
                w.radius = b.radius;
                w.swirl = b.swirl; w.updraft = b.updraft; w.inflow = b.inflow;
                w.fade = std::sin(3.14159265f * (b.age / b.life));   // 出てすぐ強く、終わりで 0
                w.pushDir[0] = b.dir.x; w.pushDir[1] = b.dir.y;
                w.push = b.push;
            }
            ++i;
        }
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_Context->Map(m_WindBurstBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, cpu, sizeof(cpu));
            m_Context->Unmap(m_WindBurstBuffer.Get(), 0);
        }
        else cpuCount = 0;
    }
    if (cpuCount == 0 && !tornadoLook.windSource) return;

    // SwarmWindSourceCS の b0 と同じ並び（48B）
    struct WindSourceCB
    {
        float footY, radiusMul, hitRadius, swirl;
        float updraft, inflow, growTime; uint32_t slotCount;
        uint32_t cpuCount, tornadoOn; float pad[2];
    } cb = {};
    static_assert(sizeof(WindSourceCB) == 48, "WindSourceCB: HLSL 側と不一致");
    cb.footY = -(m_CachedAICB.enemyRadius + m_CachedAICB.enemyCapsuleHalf);
    cb.radiusMul = tornadoLook.windRadiusMul;
    cb.hitRadius = m_CachedAICB.enemyRadius * m_CachedBomberCB.tornadoScale;
    cb.swirl = tornadoLook.windSwirl;
    cb.updraft = tornadoLook.windUpdraft;
    cb.inflow = tornadoLook.windInflow;
    cb.growTime = tornadoLook.growTime;
    cb.slotCount = Swarm::kMaxEnemies;
    cb.cpuCount = cpuCount;
    cb.tornadoOn = tornadoLook.windSource ? 1u : 0u;

    m_WindSourceCS->WriteBuffer(m_Context, 0, &cb);
    m_WindSourceCS->Bind(m_Context);
    m_WindSourceCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_WindSourceCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_WindSourceCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_WindSourceCS->SetSRV(m_Context, "cpuSources", m_WindBurstSRV.Get());
    m_WindSourceCS->SetUAV(m_Context, "windSources", m_WindSourceUAV.Get());
    m_WindSourceCS->SetUAV(m_Context, "windCount", m_WindSourceCountUAV.Get());
    m_WindSourceCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_WindSourceCS->UnbindSRVs(m_Context);
    m_WindSourceCS->UnbindUAVs(m_Context);
}

