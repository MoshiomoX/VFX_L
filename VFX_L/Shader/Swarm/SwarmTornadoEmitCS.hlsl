// ============================================================
// SwarmTornadoEmitCS.hlsl
// 生きている砂旋（SWARM_KIND_TORNADO、2026-10-19）の足元の砂埃。
// 敵スロット 1 つに 1 スレッド：寿命が進んでいる（fuse > 0）砂旋は
// 足元で TornadoDust のレシピ（VFXId::TornadoDust）の全エミッタから
// 発生レート x g_RateMul で出す。エミッタ自身の形に加えて、
// パーティクルには軸まわりの接線方向の勢い（g_Swirl）と砂旋の
// 移動速度の半分を足すので、砂はらせん状に昇りながら砂旋についていく。
//
// SwarmSystem（Swarm/SwarmSystemTornado.cpp）が草の延焼のエミッタの
// すぐ後にディスパッチし、空きプールの予約（emitBudget）を共有する。
// ============================================================
#include "../Particle/Common/EmitInit.hlsli"
#define SWARM_FRAME_CB_REG b3   // このディスパッチでは書かない。b0 は ParticleCommon の GlobalCB
#include "../Common/SwarmCommon.hlsli"

// SwarmSystem::TornadoEmitCB（b2）と一致させること
cbuffer SwarmTornadoEmitCB : register(b2)
{
    float g_FootY;        // 敵の位置 -> 足元（-(半径 + カプセルの半分)）
    float g_RateMul;      // 全エミッタの発生レートに掛ける
    uint g_DustVfx;       // VFXId::TornadoDust のレシピ番号（0 = なし）
    float g_Swirl;        // 軸まわりに足す接線方向の速さ、m/s
    float g_FootRadius;   // 砂埃が出始める輪（当たり半径）、m
    uint g_SlotCount;     // kMaxEnemies
    float2 _tePad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);
StructuredBuffer<SwarmRecipe> recipes : register(t3);
StructuredBuffer<GPUEmitter> emitters : register(t4);
Buffer<uint> deadCount : register(t5);

RWStructuredBuffer<GPUParticle> particles : register(u0);
ConsumeStructuredBuffer<uint> deadList : register(u1);
RWByteAddressBuffer emitBudget : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_SlotCount || g_DustVfx == 0u)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    SwarmEnemyExtra ex = enemyExtra[i];
    if (ex.kind != SWARM_KIND_TORNADO || ex.fuse <= 0.0)
        return;

    SwarmRecipe r = recipes[g_DustVfx];
    if (r.particleCount == 0u)
        return;

    SwarmEnemy e = enemies[i];
    float3 base = e.position + float3(0.0, g_FootY, 0.0);
    uint seed = g_BaseSeed ^ (i * 73856093u);

    for (uint ei = 0u; ei < r.particleCount; ++ei)
    {
        GPUEmitter em = emitters[r.particleStart + ei];
        em.position = base + em.position;   // エントリの位置は足元からのオフセット
        // エミッタの形は 1 m の砂旋用に作ってある：輪 / 円錐を当たり半径に合わせて拡大する
        em.shapeSize.xy *= g_FootRadius;

        uint k = StochasticCount(em.emitRate * g_RateMul, g_DeltaTime, seed);
        if (k == 0u)
            continue;

        // ---- 空きプールから予約する（このフレームの他のエミッタと共有） ----
        uint mine;
        emitBudget.InterlockedAdd(0, k, mine);
        uint freeNow = deadCount[0];
        if (mine >= freeNow)
            return;
        if (mine + k > freeNow)
            k = freeNow - mine;

        for (uint n = 0u; n < k; ++n)
        {
            uint particleIndex = deadList.Consume();
            uint s = seed + (ei * 131u + n) * 1099u;
            float3 pos = em.position;
            float3 vel = float3(0, 0, 0);
            EmitShape(em, s, pos, vel);
            // 軸まわりに旋回 + 砂旋と一緒に移動
            float2 radial = pos.xz - base.xz;
            float rl = length(radial);
            if (rl > 1e-3)
            {
                float2 tangent = float2(-radial.y, radial.x) / rl;
                vel.xz += tangent * g_Swirl;
            }
            vel += e.velocity * 0.5;
            particles[particleIndex] = InitParticleFromEmitter(em, s, pos, vel);
        }
    }
}
