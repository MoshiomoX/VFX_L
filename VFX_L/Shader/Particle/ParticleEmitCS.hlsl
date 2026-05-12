// ============================================
// ParticleEmitCS.hlsl
// 粒子発射CS ? Dead ListからConsumeして新粒子を初期化
// ============================================

#include "Common/ParticleCommon.hlsli"
#include "Common/EmitPoint.hlsli"
#include "Common/EmitSphere.hlsli"
#include "Common/EmitCone.hlsli"
#include "Common/EmitBox.hlsli"
#include "Common/EmitRing.hlsli"
#include "Common/EmitDisc.hlsli"
#include "Common/EmitMesh.hlsli"

// --- Buffer宣言 ---
StructuredBuffer<GPUEmitter> emitters : register(t0);
StructuredBuffer<EmitMeshVertex> meshVertices : register(t1);
RWStructuredBuffer<GPUParticle> particles : register(u0);
ConsumeStructuredBuffer<uint> deadList : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // 全発射器の合計発射数を超えたらreturn
    // 各EmitterのemitCountを累積して、自分がどのEmitterの何番目かを特定
    uint globalIndex = id.x;
    int emitterIndex = -1;
    int localIndex = 0;
    uint accumulated = 0;

    for (int i = 0; i < g_EmitterCount; i++)
    {
        if (!emitters[i].isActive > 0.5)
            continue;

        if (globalIndex < accumulated + (uint) emitters[i].emitCount)
        {
            emitterIndex = i;
            localIndex = globalIndex - accumulated;
            break;
        }
        accumulated += (uint) emitters[i].emitCount;
    }

    // どのEmitterにも属さない → 何もしない
    if (emitterIndex < 0)
        return;

    GPUEmitter e = emitters[emitterIndex];

    // Dead Listから空きインデックスを取得
    uint particleIndex = deadList.Consume();

    // 乱数シード初期化（フレーム毎に異なる + スレッド毎に異なる）
    uint seed = g_BaseSeed + globalIndex * 1099;

    // 形状に応じて発射位置と速度を決定
    float3 pos = e.position;
    float3 vel = float3(0, 0, 0);

    switch (e.emitType)
    {
        case 0:
            EmitPoint(e, seed, pos, vel);
            break;
        case 1:
            EmitSphere(e, seed, pos, vel);
            break;
        case 2:
            EmitCone(e, seed, pos, vel);
            break;
        case 3:
            EmitBox(e, seed, pos, vel);
            break;
        case 4:
            EmitRing(e, seed, pos, vel);
            break;
        case 5:
            EmitDisc(e, seed, pos, vel);
            break;
        case 6:
            EmitMesh(e, seed, meshVertices, pos, vel);
            break;
    }

    // 粒子初期化
    GPUParticle p = (GPUParticle) 0;

    p.position = pos;
    p.velocity = vel;
    p.acceleration = e.gravity;
    p.drag = e.dragCoeff;

    p.lifetime = RandomRange(seed, e.lifetimeRange.x, e.lifetimeRange.y);
    p.age = 0.0;
    p.isAlive = 1.0;

    p.startColor = RandomColor(seed, e.startColorMin, e.startColorMax);
    p.endColor = RandomColor(seed, e.endColorMin, e.endColorMax);
    p.color = p.startColor;

    p.startSize = RandomRange(seed, e.sizeRange.x, e.sizeRange.y);
    p.endSize = RandomRange(seed, e.sizeRange.z, e.sizeRange.w);
    p.size = p.startSize;

    p.rotation = RandomRange(seed, e.rotationRange.x, e.rotationRange.y);
    p.angularVel = RandomRange(seed, e.angularVelRange.x, e.angularVelRange.y);

    p.uvFrame = 0;
    p.seed = seed;

    particles[particleIndex] = p;
}