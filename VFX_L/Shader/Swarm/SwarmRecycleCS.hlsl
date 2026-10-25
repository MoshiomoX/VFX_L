// ============================================================
// SwarmRecycleCS.hlsl
// Pool is full but the director still wants spawns: instead of
// killing the farthest enemies and re-spawning, far-enough alive
// enemies claim a pending request and overwrite themselves with
// it (position, hp, speed -- everything). No sort, no free slot.
//
// "Far enough" is a distance threshold, not "the N farthest".
// First come first served among those past the threshold; if
// nobody is far enough the requests are simply dropped, which is
// the intended behaviour when the whole crowd is on the player.
// ============================================================
#define SWARM_AI_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> spawnRequests : register(t0);
Buffer<uint> enemyStates : register(t1);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer claim : register(u1);
// spawn hp per slot for the HP bar (see SwarmSpawnEnemyCS)
RWStructuredBuffer<uint> enemyMaxHp : register(u2);
// kind + fuse per slot (see SwarmSpawnEnemyCS). A lit bomber that gets
// recycled forgets its fuse
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u3);
// poison slow per slot: a recycled enemy is a new one, unslowed
RWStructuredBuffer<float4> enemySlow : register(u4); // slow + freeze (float4 since 2026-10-08)
RWStructuredBuffer<float4> enemyPush : register(u5); // knockback (the Gust, 2026-10-12): cleared with the slow
RWStructuredBuffer<float4> enemyPoison : register(u6); // stored toxin (2026-10-17): cleared too
// the damage numbers' hp of last frame (SwarmDamageTextCS, 2026-10-25): the new spawn hp + SWARM_PREVHP_RECYCLED, so the
// old hp minus the new one is not printed as damage here and the old enemy's pending numbers are dropped.
// (No UAV is left for the state word: its element / lifesteal bits stay until the new enemy's first hit rewrites them,
// and every hp loss comes with that rewrite, the toxin having been cleared above)
RWBuffer<uint> prevHp : register(u7);

cbuffer SwarmRecycleCB : register(b1)
{
    uint g_RecycleCount;
    uint g_RecycleOffset;
    float g_RecycleMinDistSq;
    uint _recyclePad;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    float3 d = enemies[i].position - g_PlayerPos;
    float dSq = d.x * d.x + d.z * d.z;
    if (dSq < g_RecycleMinDistSq)
        return;
    // elites / the boss are never recycled into a plain mob: they keep chasing.
    // Ghosts neither (they fly straight in). Splitters and splitlings can be (2026-10-03: they are
    // numbered after the ghost, so the old "kind >= ELITE" test would have pinned them far away)
    uint kind = enemyExtra[i].kind;
    if (SwarmIsBigElite(kind) || kind == SWARM_KIND_BOSS || SwarmIsGhost(kind) || kind == SWARM_KIND_TORNADO)
        return;

    uint prev;
    claim.InterlockedAdd(0, 1u, prev);
    if (prev >= g_RecycleCount)
        return;

    SwarmEnemy req = spawnRequests[g_RecycleOffset + prev];
    SwarmEnemyExtra extra;
    extra.kind = req.animIndex;
    extra.fuse = 0.0;
    req.animIndex = 0u;
    enemies[i] = req;
    enemyMaxHp[i] = req.hp;
    enemyExtra[i] = extra;
    enemySlow[i] = float4(0, 0, 0, 0);
    enemyPush[i] = float4(0, 0, 0, 0);
    enemyPoison[i] = float4(0, 0, 0, 0);
    prevHp[i] = SWARM_PREVHP_RECYCLED | (req.hp & ~SWARM_PREVHP_RECYCLED);
}