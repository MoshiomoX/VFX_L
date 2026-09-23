// ============================================================
// SwarmHitCS.hlsl
// Projectile vs enemy, brute force. One thread per projectile,
// loop over every enemy slot. Runs after ProjMoveCS in the fixed
// step, so positions are this step's final values.
//
// Damage is applied with InterlockedAdd on the fixed-point hp,
// so any number of projectiles may hit the same enemy in the same
// step. Exactly one of them observes hp crossing zero and becomes
// the killer; the rest see either a live enemy or a corpse.
//
// prev (hp before the add) tells the thread what it hit:
//   prev == 0 or prev >= 0x80000000  -> corpse (already dead / wrapped)
//   prev <= dmg                      -> this hit killed it
//   otherwise                        -> still alive
//
// No pierce yet: a projectile dies on its first live hit.
// Corpses are passed through without consuming the projectile.
// ============================================================
// the hit may leave an area behind (explosion / burning ground)
#define SWARM_AREA_POOL_U u6
#define SWARM_AREA_STATE_U u7
#define SWARM_AREA_DEF_T t2
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmMotion> motions : register(t1);

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
RWBuffer<uint> projStates : register(u0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u1);
RWBuffer<uint> enemyStates : register(u2);
RWByteAddressBuffer counters : register(u3);
RWStructuredBuffer<SwarmOrb> orbs : register(u4);
RWBuffer<uint> orbStates : register(u5);
static const uint HP_CORPSE_BIT = 0x80000000u;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;
    if (projStates[i] == SWARM_DEAD)
        return;

    SwarmProjectile p = projectiles[i];

    float hitRadius = p.radius + g_EnemyRadius;
    float hitRadiusSq = hitRadius * hitRadius;
    uint dmgFixed = SwarmHpToFixed(p.damage);

    for (uint j = 0; j < g_MaxEnemies; ++j)
    {
        if (enemyStates[j] == SWARM_DEAD)
            continue;

        float3 d = p.position - enemies[j].position;
        d.y -= clamp(d.y, -g_EnemyCapsuleHalf, g_EnemyCapsuleHalf);
        if (dot(d, d) > hitRadiusSq)
            continue;
        // ---- hit: apply damage atomically ----
        uint prev;
        InterlockedAdd(enemies[j].hp, (uint) (-(int) dmgFixed), prev);

        if (prev == 0u || prev >= HP_CORPSE_BIT)
            continue; // corpse. someone else already killed it this step

        // ---- hit stun: freeze + flash (MoveCS / AICS / VS read it) ----
        // plain stores: several hits in one step all write the same value.
        // harmless on the killer, its slot goes DEAD right below
        enemies[j].animIndex = 2u;
        enemies[j].animTime = 0.0;

        if (prev <= dmgFixed)
        {
            // ---- this hit was the killer ----
            enemyStates[j] = SWARM_DEAD;
            counters.InterlockedAdd(SWARM_CNT_KILLS, 1u, prev);

                        // ---- drop an exp orb at the corpse ----
            // Same CAS scan as SpawnProjCS. Start offset is spread by
            // (enemy, projectile) so killers in the same step do not
            // all fight over the same region. Pool full -> no orb.
            SwarmOrb orb;
            orb.position = enemies[j].position;
            orb.position.y = g_OrbY;
            orb.amount = g_OrbAmount;
            orb.velocity = float3(0, 0, 0);
            orb._pad = 0.0; // pull speed, 0 = not attracted yet

            uint start = (j * 97u + i * 31u) % g_MaxOrbs;
            for (uint k = 0; k < g_MaxOrbs; ++k)
            {
                uint slot = (start + k) % g_MaxOrbs;
                uint was;
                InterlockedCompareExchange(orbStates[slot],
                                           SWARM_DEAD, SWARM_ALIVE, was);
                if (was == SWARM_DEAD)
                {
                    orbs[slot] = orb;
                    break;
                }
            }
        }

        // ---- area on hit (explosion etc.). Ticks in this same step:
        // AreaTickCS / AreaDamageCS run right after this shader ----
        SwarmSpawnAreaFromDef(motions[p.motion & SWARM_MOTION_INDEX_MASK].hitArea,
                              p.position, i + j);

        // projectile is consumed either way (no pierce)
        projStates[i] = SWARM_DEAD;
        return;
    }
}