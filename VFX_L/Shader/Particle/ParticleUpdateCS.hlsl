// ============================================
// ParticleUpdateCS.hlsl
// 粒子更新CS — 寿命処理 / 物理 / 色補間 / AliveList 構築
//
// 所有者別の生存数もここで数える（VFX状態機の Finishing 判定用）
// ============================================

#include "Common/ParticleCommon.hlsli"
#include "../Common/WindSources.hlsli"

#define PARTICLE_MAX_OWNERS 1024

// World wind (2026-10-12): particles whose renderMode bits 26-29 are set
// drift toward the wind (smoke / mist / rain / sand; sparks stay 0).
// Must match GPUParticleSystem::ParticleWindCB (b2; b0 / b1 are ParticleCommon's)
cbuffer ParticleWindCB : register(b2)
{
    float2 g_WindDir;       // unit xz, the way the wind blows
    float g_WindSpeed;      // m/s (base), already times the weather strength
    float g_WindGust;       // m/s (gust amplitude), same
    float g_WindWaveScale;  // gust waves per meter
    float g_WindWaveSpeed;  // radians per second
    float g_WindTime;       // the grass's wind clock (same waves as the blades)
    float _windPad;
};

// world wind at xz (the GrassVS gust formula)
float2 ParticleWindAt(float2 xz)
{
    float phase = dot(xz, g_WindDir) * g_WindWaveScale * 6.2831853 - g_WindTime * g_WindWaveSpeed;
    float w = sin(phase) * 0.5 + 0.5;
    w *= w;
    return g_WindDir * (g_WindSpeed + g_WindGust * w);
}

StructuredBuffer<ColorKey> colorKeys : register(t0);
StructuredBuffer<WindSource> g_WindSources : register(t1);   // local wind: the sand tornadoes (SwarmWindSourceCS, 2026-10-19)
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

    // wind: relax the horizontal velocity toward the world wind (smoke drifts, sparks don't)
    float windResp = ParticleWindResponse(p.renderMode);
    if (windResp > 0.0)
    {
        // a wind streak (2026-10-20) is the air itself: it takes the local air speed about
        // five times faster than smoke, so the spiral of a vortex shows within 0.1 s
        float rate = ParticleIsStreak(p.renderMode) ? PARTICLE_WIND_RATE * 5.0 : PARTICLE_WIND_RATE;
        float k = saturate(windResp * rate * g_DeltaTime);
        float vortexLift;
        float3 vortexAir = WindSourceAir(g_WindSources, p.position.xz, vortexLift);
        float2 wind = ParticleWindAt(p.position.xz) + vortexAir.xz;
        p.velocity.xz += (wind - p.velocity.xz) * k;
        if (vortexLift > 0.0)   // the updraft only inside a source (no pull toward 0 elsewhere)
            p.velocity.y += (vortexAir.y - p.velocity.y) * k * vortexLift;
    }

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