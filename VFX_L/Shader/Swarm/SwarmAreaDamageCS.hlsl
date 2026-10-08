// ============================================================
// SwarmAreaDamageCS.hlsl
// Area damage. One thread per ENEMY, loop over the area slots.
//
// Thread-per-enemy (not per-area) on purpose: every area that ticks
// this step is summed first and hp is touched once, by the only
// thread that owns this enemy in this dispatch. So there is no race
// on hp here at all, and one enemy standing in three circles is
// killed once, drops one orb, counts as one kill.
//
// When no area ticks this step (almost every step) each thread
// returns after a single counter load.
//
// Kill handling mirrors SwarmHitCS: state -> DEAD, kill counter,
// exp orb. Keep the two in sync (elite body size and orb value too).
//
// A ticking area with slow bits (SwarmAreaSlow) also refreshes the
// enemy's enemySlow entry (2026-10-01, the Poison pool).
// The shield bearer's armor comes off every area's tick separately
// (SwarmArmorDamage, 2026-10-08): three pools are three small hits.
// ============================================================
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);
StructuredBuffer<float4> areaEnds : register(t3); // capsule (beam) end per slot, written by AreaTickCS

RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWBuffer<uint> enemyStates : register(u1);
RWByteAddressBuffer counters : register(u2);
RWStructuredBuffer<SwarmOrb> orbs : register(u3);
RWBuffer<uint> orbStates : register(u4);
// x / y = slow (seconds left, amount). AICS reads and counts it down. z / w = freeze (left untouched here)
RWStructuredBuffer<float4> enemySlow : register(u5);

static const uint HP_CORPSE_BIT = 0x80000000u;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint j = id.x;
    if (j >= g_MaxEnemies)
        return;
    if (counters.Load(SWARM_CNT_TICKING_AREAS) == 0u)
        return;
    if (enemyStates[j] == SWARM_DEAD)
        return;

    float3 epos = enemies[j].position;
    SwarmEnemyExtra extra = enemyExtra[j];
    uint kind = extra.kind;
    float3 ecenter;
    float er, eh;
    SwarmKindCapsule(kind, epos, ecenter, er, eh);

    float total = 0.0;
    bool stun = false;
    float slow = 0.0;     // strongest slow among the areas this enemy stands in
    float slowTime = 0.0; // until the next tick of that area, plus a little

    for (uint k = 0; k < SWARM_MAX_AREAS; ++k)
    {
        if (areaStates[k] == SWARM_DEAD)
            continue;

        SwarmArea a = areas[k];
        if (a.tickNow == 0u)
            continue;

        // capsule (beam): measure from the closest point of the segment centre -> end
        float3 c = a.center;
        if ((a.flags & SWARM_AREA_CAPSULE) != 0u)
        {
            float3 seg = areaEnds[k].xyz - a.center;
            float t = saturate(dot(ecenter - a.center, seg) / max(dot(seg, seg), 1e-4));
            c = a.center + seg * t;
        }
        float3 d = ecenter - c;

        // vertical: outside the slab + the capsule's straight part -> miss
        if (abs(d.y) > a.halfHeight + eh + er)
            continue;

        float reach = a.radius + er;
        if (d.x * d.x + d.z * d.z > reach * reach)
            continue;

        total += SwarmArmorDamage(kind, a.damage);
        if ((a.flags & SWARM_AREA_STUN) != 0u)
            stun = true;
        float s = SwarmAreaSlow(a.flags);
        if (s > 0.0)
        {
            slow = max(slow, s);
            slowTime = max(slowTime, min(a.tickInterval, 2.0) + SWARM_SLOW_LINGER);
        }
    }

    // ---- slow: refresh, never weaken one that is still running ----
    if (slow > 0.0)
    {
        if (kind == SWARM_KIND_ELITE || kind == SWARM_KIND_BOSS)
            slow *= SWARM_SLOW_BIG_MUL;
        float4 cur = enemySlow[j];
        if (cur.x <= 0.0)
            cur.y = 0.0;
        cur.xy = float2(max(cur.x, slowTime), max(cur.y, slow));
        enemySlow[j] = cur;
    }

    if (total <= 0.0)
        return;

    uint dmgFixed = SwarmHpToFixed(total);

    uint prev;
    InterlockedAdd(enemies[j].hp, (uint) (-(int) dmgFixed), prev);
    if (prev == 0u || prev >= HP_CORPSE_BIT)
        return; // already a corpse

    // the boss never flinches, nor does a charger that has started its wind-up (see HitCS)
    bool noFlinch = (kind == SWARM_KIND_BOSS) || (kind == SWARM_KIND_CHARGER && extra.fuse > 0.0);
    if (stun && !noFlinch)
    {
        enemies[j].animIndex = 2u;
        enemies[j].animTime = 0.0;
    }

    if (prev <= dmgFixed)
    {
        // ---- killed by the area ----
        enemyStates[j] = SWARM_DEAD;
        counters.InterlockedAdd(SWARM_CNT_KILLS, 1u, prev);

        SwarmOrb orb;
        orb.position = epos;
        orb.position.y = epos.y - g_GroundY + g_OrbY; // at the corpse's feet (see SwarmHitCS)
        orb.amount = g_OrbAmount * SwarmKindExpMul(kind);
        orb.velocity = float3(0, 0, 0);
        orb._pad = 0.0;

        uint start = (j * 97u) % g_MaxOrbs;
        for (uint s = 0; s < g_MaxOrbs; ++s)
        {
            uint slot = (start + s) % g_MaxOrbs;
            uint was;
            InterlockedCompareExchange(orbStates[slot], SWARM_DEAD, SWARM_ALIVE, was);
            if (was == SWARM_DEAD)
            {
                orbs[slot] = orb;
                break;
            }
        }
    }
}
