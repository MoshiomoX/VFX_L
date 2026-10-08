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
//
// Elites / the boss have a bigger capsule (SwarmKindCapsule) and drop an
// orb worth SwarmKindExpMul times more. The kind is only read for enemies
// that pass a coarse test sized for the biggest body.
// The shield bearer takes SwarmArmorDamage; a shot whose motion has a
// freezeTime (Ice Lance) leaves a freeze request in animIndex (2026-10-08).
// ============================================================
// the hit may leave an area behind (explosion / burning ground)
#define SWARM_AREA_POOL_U u6
#define SWARM_AREA_STATE_U u7
#define SWARM_AREA_DEF_T t2
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmMotion> motions : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);
Buffer<uint> projBoost : register(t4); // damage / duration boost of the area this shot leaves

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

    // a falling meteor (DROP) / a thrown poison flask (LOB) does not hit anything
    // on the way. ProjMoveCS leaves its area where it lands
    if (SwarmMotionLands(motions[p.motion & SWARM_MOTION_INDEX_MASK].mode))
        return;

    // coarse sphere around the pool position that holds the biggest body
    float kMax = max(1.0, max(g_EliteScale, g_BossScale));
    float coarse = p.radius + kMax * 2.0 * (g_EnemyRadius + g_EnemyCapsuleHalf);
    float coarseSq = coarse * coarse;
    // Ice Lance (2026-10-08): the enemy it hits freezes for this long (AICS takes the request)
    float freezeTime = motions[p.motion & SWARM_MOTION_INDEX_MASK].freezeTime;

    for (uint j = 0; j < g_MaxEnemies; ++j)
    {
        if (enemyStates[j] == SWARM_DEAD)
            continue;

        float3 pos = enemies[j].position;
        float3 dc = p.position - pos;
        if (dot(dc, dc) > coarseSq)
            continue;

        SwarmEnemyExtra extra = enemyExtra[j];
        uint kind = extra.kind;
        float3 center;
        float er, eh;
        SwarmKindCapsule(kind, pos, center, er, eh);
        float3 d = p.position - center;
        d.y -= clamp(d.y, -eh, eh);
        float hitRadius = p.radius + er;
        if (dot(d, d) > hitRadius * hitRadius)
            continue;
        // ---- hit: apply damage atomically (the shield bearer's armor first) ----
        uint dmgFixed = SwarmHpToFixed(SwarmArmorDamage(kind, p.damage));
        uint prev;
        InterlockedAdd(enemies[j].hp, (uint) (-(int) dmgFixed), prev);

        if (prev == 0u || prev >= HP_CORPSE_BIT)
            continue; // corpse. someone else already killed it this step

        // ---- hit stun: freeze + flash (MoveCS / AICS / VS read it) ----
        // plain stores: several hits in one step all write the same value.
        // harmless on the killer, its slot goes DEAD right below.
        // The boss never flinches (it would be pinned by the constant fire), nor does a
        // charger that has started its wind-up (MoveCS would stop the dash on every hit).
        // An ice shot leaves a freeze request instead of the stun
        bool noFlinch = (kind == SWARM_KIND_BOSS) || (kind == SWARM_KIND_CHARGER && extra.fuse > 0.0);
        if (freezeTime > 0.0)
        {
            enemies[j].animIndex = SWARM_ANIM_FREEZE_REQ;
            enemies[j].animTime = freezeTime;
        }
        else if (!noFlinch)
        {
            enemies[j].animIndex = 2u;
            enemies[j].animTime = 0.0;
        }

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
            // at the corpse's feet (plateaus: position.y - groundY = terrain height)
            orb.position = enemies[j].position;
            orb.position.y = enemies[j].position.y - g_GroundY + g_OrbY;
            orb.amount = g_OrbAmount * SwarmKindExpMul(kind);
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
                              p.position, i + j, SwarmProjScale(p), projBoost[i], p.velocity.xz);

        // projectile is consumed either way (no pierce)
        projStates[i] = SWARM_DEAD;
        return;
    }
}