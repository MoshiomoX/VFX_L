// ============================================
// ParticleUpdateCS.hlsl
// 粒子更新CS — 寿命処理 / 物理 / 色補間 / AliveList 構築
//
// 所有者別の生存数もここで数える（VFX状態機の Finishing 判定用）
// ============================================

#include "Common/ParticleCommon.hlsli"

#define PARTICLE_MAX_OWNERS 1024

StructuredBuffer<ColorKey> colorKeys : register(t0);
RWStructuredBuffer<GPUParticle> particles : register(u0);
AppendStructuredBuffer<uint> deadList : register(u1);
RWBuffer<uint> g_DrawArgs : register(u2);
RWStructuredBuffer<uint> aliveList : register(u3); // 存活粒子 index リスト (billboard)
RWBuffer<uint> ownerAlive : register(u4); // 所有者ごとの生存数
RWStructuredBuffer<uint> aliveMesh : register(u5); // alive mesh particles, PARTICLE_MESH_BUCKET_CAP per bucket
RWBuffer<uint> meshCounts : register(u6); // particles per bucket (copied into each submesh's draw args)

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_MaxParticles)
        return;

    GPUParticle p = particles[id.x];

    if (p.isAlive < 0.5)
        return;

    p.age += g_DeltaTime;

    if (p.age >= p.lifetime)
    {
        p.isAlive = 0.0;
        deadList.Append(id.x);
        particles[id.x] = p;
        return;
    }

    // --- 生存確定：DrawArgs に +1、AliveList に index を追加 ---
    // renderMode で billboard / mesh に振り分ける（描画パスが別）
    uint instanceIndex;
    if (ParticleMeshSlotPlusOne(p.renderMode) != 0u)
    {
        // mesh: one segment of aliveMesh per bucket (mesh index x lit/glow).
        // Past the cap the count keeps growing but nothing is written;
        // ParticleMeshVS drops instances >= cap
        uint bucket = ParticleMeshBucket(p.renderMode);
        InterlockedAdd(meshCounts[bucket], 1, instanceIndex);
        if (instanceIndex < PARTICLE_MESH_BUCKET_CAP)
            aliveMesh[bucket * PARTICLE_MESH_BUCKET_CAP + instanceIndex] = id.x;
    }
    else
    {
        InterlockedAdd(g_DrawArgs[1], 1, instanceIndex);
        aliveList[instanceIndex] = id.x;
    }

    // --- 所有者別に数える（死んだ粒子は数えない）---
    // ※範囲外/負値は 0（無主バケツ）へ落とす。
    //   clamp で MAX-1 に寄せると正規の所有者と混ざるので不可。
    uint oid = (p.ownerID >= 0 && p.ownerID < PARTICLE_MAX_OWNERS)
             ? (uint) p.ownerID : 0u;
    InterlockedAdd(ownerAlive[oid], 1);

    // --- 以下は既存のまま ---
    float t = GetLifeRatio(p);

    // atlasAnimate = (first << 12) | count (see InitParticleFrame)
    if (p.atlasAnimate > 0)
    {
        int count = p.atlasAnimate & 0xFFF;
        int first = p.atlasAnimate >> 12;
        p.uvFrame = first + min((int) (t * count), count - 1);
    }

    p.velocity += p.acceleration * g_DeltaTime;
    p.velocity *= (1.0 - p.drag * g_DeltaTime);
    p.position += p.velocity * g_DeltaTime;

    if (p.colorKeyCount > 0)
    {
        float4 result = colorKeys[p.colorKeyOffset].color;
        for (int k = 1; k < p.colorKeyCount; k++)
        {
            ColorKey prev = colorKeys[p.colorKeyOffset + k - 1];
            ColorKey next = colorKeys[p.colorKeyOffset + k];
            if (t >= prev.time && t <= next.time)
            {
                float localT = (t - prev.time) / (next.time - prev.time);
                result = lerp(prev.color, next.color, localT);
                break;
            }
            if (t > next.time)
                result = next.color;
        }
        p.color = result;
    }
    else
    {
        p.color = lerp(p.startColor, p.endColor, t);
    }

    p.size = lerp(p.startSize, p.endSize, t);
    p.rotation += p.angularVel * g_DeltaTime;
    p.rot3 += p.angVel3 * g_DeltaTime;
    particles[id.x] = p;
}