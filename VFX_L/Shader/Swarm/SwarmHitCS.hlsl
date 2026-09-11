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
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
RWBuffer<uint> projStates : register(u0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u1);
RWBuffer<uint> enemyStates : register(u2);
RWByteAddressBuffer counters : register(u3);

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

        if (prev <= dmgFixed)
        {
            // ---- this hit was the killer ----
            enemyStates[j] = SWARM_DEAD;
            counters.InterlockedAdd(SWARM_CNT_KILLS, 1u, prev);

            // Phase 4 step 3: spawn an exp orb here (claim an orb slot
            // with the same CAS scan as SpawnProjCS, position = enemy)
        }

        // projectile is consumed either way (no pierce)
        projStates[i] = SWARM_DEAD;
        return;
    }
}