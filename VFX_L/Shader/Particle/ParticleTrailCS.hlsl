// ============================================================
// ParticleTrailCS.hlsl
// ParticleUpdateCS の直後に走る。パーティクルスロット 1 つに 1 スレッド。
//
// トレイルのスタイルを持つ生きているパーティクルごとに：
//   - パーティクルの年齢がサンプリング間隔の次の点を越えたら、
//     現在の位置をリングに入れる
//   - トレイルの生存リスト（DrawInstancedIndirect の引数）に追加する
//
// わざと UpdateCS から分けている：あちらは既に 7 本の UAV を使っていて、
// D3D11.0 の上限は 8 本。
// ============================================================
#include "Common/ParticleCommon.hlsli"
#include "Common/ParticleTrail.hlsli"

StructuredBuffer<TrailStyle> trailStyles : register(t0);

RWStructuredBuffer<GPUParticle> particles : register(u0);
RWStructuredBuffer<float3> trailPoints : register(u1);
RWStructuredBuffer<uint> trailAlive : register(u2);
RWBuffer<uint> g_TrailArgs : register(u3); // [1] = InstanceCount

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxParticles)
        return;

    GPUParticle p = particles[i];
    if (p.isAlive < 0.5 || p.trailStyle == 0u)
        return;

    TrailStyle s = trailStyles[p.trailStyle - 1u];
    float interval = TrailInterval(s.lifetime);

    uint head = TrailHead(p.trailState);
    uint count = TrailCount(p.trailState);

    // このフレームの age は UpdateCS が既に g_DeltaTime だけ進めている
    uint cellNow = (uint) (p.age / interval);
    uint cellPrev = (uint) (max(p.age - g_DeltaTime, 0.0) / interval);

    // count == 0：このパーティクルを初めて見た -> 生まれた位置を記録する
    if (count == 0u || cellNow != cellPrev)
    {
        head = (count == 0u) ? 0u : (head + 1u) % TRAIL_POINTS;
        trailPoints[i * TRAIL_POINTS + head] = p.position;
        count = min(count + 1u, TRAIL_POINTS);
        particles[i].trailState = TrailPack(head, count);
    }

    uint slot;
    InterlockedAdd(g_TrailArgs[1], 1u, slot);
    trailAlive[slot] = i;
}
