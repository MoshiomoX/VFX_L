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
// ============================================================
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);

RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWBuffer<uint> enemyStates : register(u1);
RWByteAddressBuffer counters : register(u2);
RWStructuredBuffer<SwarmOrb> orbs : register(u3);
RWBuffer<uint> orbStates : register(u4);

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
    uint kind = enemyExtra[j].kind;
    float3 ecenter;
    float er, eh;
    SwarmKindCapsule(kind, epos, ecenter, er, eh);

    float total = 0.0;
    bool stun = false;

    for (uint k = 0; k < SWARM_MAX_AREAS; ++k)
    {
        if (areaStates[k] == SWARM_DEAD)
            continue;

        SwarmArea a = areas[k];
        if (a.tickNow == 0u)
            continue;

        float3 d = ecenter - a.center;

        // vertical: outside the slab + the capsule's straight part -> miss
        if (abs(d.y) > a.halfHeight + eh + er)
            continue;

        float reach = a.radius + er;
        if (d.x * d.x + d.z * d.z > reach * reach)
            continue;

        total += a.damage;
        if ((a.flags & SWARM_AREA_STUN) != 0u)
            stun = true;
    }

    if (total <= 0.0)
        return;

    uint dmgFixed = SwarmHpToFixed(total);

    uint prev;
    InterlockedAdd(enemies[j].hp, (uint) (-(int) dmgFixed), prev);
    if (prev == 0u || prev >= HP_CORPSE_BIT)
        return; // already a corpse

    if (stun && kind != SWARM_KIND_BOSS)   // the boss never flinches
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
