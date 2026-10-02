// ============================================================
// SwarmCorpseTrackCS.hlsl
// Once per frame, one thread per enemy slot (2026-10-02, death shatter).
// A slot that was alive last frame (prevAlive) and is DEAD now died since
// the last frame: its last position / yaw / kind are still in the slot
// (nothing clears a dead slot until it is reused), so they are copied into
// the corpse ring for SwarmCorpseVS, and a dust puff area is left at its
// feet (g_DeathArea, the AreaDef "MobDeath": damage 0, look only).
//
// Done here instead of in the killers (HitCS / AreaDamageCS / ContactCS):
// HitCS already has all 8 UAVs of D3D11.0 in use, and one place catches
// every kind of death the same way. KillAll clears prevAlive as well, so
// wiping the field does not shatter everything at once.
//
// A slot that dies and is reused within the same frame is missed (rare).
// ============================================================
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_AREA_POOL_U u3
#define SWARM_AREA_STATE_U u4
#define SWARM_AREA_DEF_T t3
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmCorpseTrackCB : register(b4)
{
    float g_CorpseTime;   // SwarmSystem's clock (m_AnimClock)
    uint g_CorpseOn;      // 0 = only keep prevAlive up to date
    uint g_DeathArea;     // AreaDef of the dust puff, 0 = none
    uint _corpseTrackPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);

RWBuffer<uint> prevAlive : register(u0);
RWStructuredBuffer<SwarmCorpse> corpses : register(u1);
RWByteAddressBuffer corpseHead : register(u2);   // [0] = corpses written so far (wraps the ring)

uint CorpseHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;

    uint alive = (enemyStates[i] != SWARM_DEAD) ? 1u : 0u;
    uint was = prevAlive[i];
    prevAlive[i] = alive;
    if (was == 0u || alive != 0u || g_CorpseOn == 0u)
        return;

    // ---- died since last frame ----
    SwarmEnemy e = enemies[i];
    uint n;
    corpseHead.InterlockedAdd(0, 1u, n);

    SwarmCorpse c;
    c.position = e.position;
    c.yaw = e.yaw;
    float2 away = e.position.xz - g_PlayerPos.xz;
    float awayLenSq = dot(away, away);
    // away from the player (the spells come from there); right on top of the player: backwards
    c.dir = (awayLenSq > 1e-4) ? away * rsqrt(awayLenSq) : -float2(sin(e.yaw), cos(e.yaw));
    c.birth = g_CorpseTime;
    c.kind = enemyExtra[i].kind;
    c.seed = CorpseHash(i * 2654435761u ^ (n * 0x9E3779B9u));
    c.cause = 0u;
    c._pad = float2(0.0, 0.0);
    corpses[n % SWARM_MAX_CORPSES] = c;

    if (g_DeathArea != 0u)
        SwarmSpawnAreaFromDef(g_DeathArea, e.position, i, 1.0, 0u, float2(0.0, 0.0));
}
