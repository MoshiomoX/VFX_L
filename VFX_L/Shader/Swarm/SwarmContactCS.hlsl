// ============================================================
// SwarmContactCS.hlsl
// Enemy touches player -> damage. One thread per enemy slot.
//
// Both bodies are vertical capsules, so the test is: XZ circle
// distance combined with the vertical gap between the two straight
// segments. Jumping over the crowd clears the vertical gap and
// lands no hit, which is the intended read.
//
// Each enemy has its own cooldown so a crowd of 30 does not deal
// 30x per step. Damage goes into the counter as fixed point x100;
// the CPU takes the delta and feeds it to the player state machine,
// which owns invincibility frames.
//
// Bombers (enemyExtra.kind) do not melee. Touching the player lights
// the fuse; g_BomberFuseTime later the bomber blows up: the player takes
// g_BomberBlastDamage if still inside g_BomberBlastRadius, the slot goes
// DEAD (no kill count, no orb: the player did not kill it) and a damage-0
// area is left behind only for the GPU explosion VFX. A bomber killed
// during the fuse dies in HitCS / AreaDamageCS first (normal kill + orb)
// and never reaches this pass lit.
//
// Every hit also records where it came from in playerHits (u6) so the
// CPU can knock the player back (melee a little, blasts ~3x + a hop).
//
// A frozen enemy (enemySlow.z > 0, Ice Lance) does not hit or light its fuse.
// A dashing charger hits g_ChargerDashDamageMul harder and knocks the player
// back like a blast (2026-10-08).
//
// Only writer of Enemy.attackCooldown and a bomber's SwarmEnemyExtra.fuse
// (a charger's fuse belongs to AICS).
// ============================================================
#define SWARM_AREA_POOL_U u4
#define SWARM_AREA_STATE_U u5
#define SWARM_AREA_DEF_T t0
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<float4> enemySlow : register(t1); // z > 0 = frozen

RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer counters : register(u1);
RWBuffer<uint> enemyStates : register(u2);
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u3);
// direction of the hits the player took, for the CPU knockback (2026-10-01).
// Swarm::PlayerHitInfo: melee x, z, count, blast x, z, count (+ 2 pad).
// x / z = sum of unit vectors (enemy or blast centre -> player) * HIT_DIR_SCALE.
// Never cleared: the CPU diffs it like the counters
RWByteAddressBuffer playerHits : register(u6);
static const float HIT_DIR_SCALE = 1000.0; // = Swarm::kHitDirScale

// add one hit coming from `from` to the record at byte offset `base`
void RecordHit(uint base, float3 from)
{
    float2 d = g_PlayerPos.xz - from.xz;
    float len = length(d);
    d = (len > 1e-4) ? d / len : float2(0.0, 0.0);
    uint prev;
    playerHits.InterlockedAdd(base + 0u, asuint((int) round(d.x * HIT_DIR_SCALE)), prev);
    playerHits.InterlockedAdd(base + 4u, asuint((int) round(d.y * HIT_DIR_SCALE)), prev);
    playerHits.InterlockedAdd(base + 8u, 1u, prev);
}

// The AI pass stops an enemy just OUTSIDE (enemyRadius + playerRadius),
// so an exact-radius test never fires while the player stands still.
// The resting gap is about 1 cm; the skin covers it with margin.
static const float CONTACT_SKIN = 0.05;

// player capsule within `reach` of an enemy capsule (XZ + vertical gap).
// halfLen = the enemy capsule's straight half (bigger for elites)
bool PlayerWithinCapsule(float3 pos, float halfLen, float reach)
{
    float dx = pos.x - g_PlayerPos.x;
    float dz = pos.z - g_PlayerPos.z;
    float dxzSq = dx * dx + dz * dz;

    // vertical gap between the two straight segments (0 if they overlap)
    float dy = abs(pos.y - g_PlayerPos.y);
    float gap = max(0.0, dy - (halfLen + g_PlayerCapsuleHalf));
    return dxzSq + gap * gap <= reach * reach;
}

bool PlayerWithin(float3 pos, float reach)
{
    return PlayerWithinCapsule(pos, g_EnemyCapsuleHalf, reach);
}

void UpdateBomber(uint i, SwarmEnemyExtra extra)
{
    float3 pos = enemies[i].position;

    if (extra.fuse <= 0.0)
    {
        // ---- not lit: light it on contact ----
        float reach = g_EnemyRadius + g_PlayerRadius + g_BomberTriggerMargin;
        if (g_PlayerAlive != 0u && PlayerWithin(pos, reach))
            enemyExtra[i].fuse = g_Step;
        return;
    }

    // ---- lit: count up, blow up at the end ----
    // keeps burning even if the player died meanwhile (the blast just hits nobody)
    float fuse = extra.fuse + g_Step;
    if (fuse < g_BomberFuseTime)
    {
        enemyExtra[i].fuse = fuse;
        return;
    }

    if (g_PlayerAlive != 0u && PlayerWithin(pos, g_BomberBlastRadius + g_PlayerRadius))
    {
        uint prev;
        counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE,
                                SwarmHpToFixed(g_BomberBlastDamage), prev);
        RecordHit(12u, pos);   // blast: from the bomber
    }
    enemyStates[i] = SWARM_DEAD;
    enemyExtra[i].fuse = 0.0;
    SwarmSpawnAreaFromDef(g_BomberBlastArea, pos, i, 1.0, 0u, float2(0.0, 0.0));
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    SwarmEnemyExtra extra = enemyExtra[i];
    bool frozen = enemySlow[i].z > 0.0;
    if (extra.kind == SWARM_KIND_BOMBER)
    {
        // frozen: a lit fuse keeps burning (it is a bomb), an unlit one is not lit by touching
        if (!frozen || extra.fuse > 0.0)
            UpdateBomber(i, extra);
        return;
    }

    float cd = enemies[i].attackCooldown - g_Step;

    if (cd <= 0.0 && g_PlayerAlive != 0u && !frozen)
    {
        // elites / boss: bigger capsule, harder hits
        float3 center;
        float er, eh;
        SwarmKindCapsule(extra.kind, enemies[i].position, center, er, eh);
        float reach = er + g_PlayerRadius + CONTACT_SKIN;
        if (PlayerWithinCapsule(center, eh, reach))
        {
            bool dash = (extra.kind == SWARM_KIND_CHARGER)
                     && (SwarmChargerPhase(extra.fuse) == SWARM_CHARGE_DASH);
            float dmg = g_ContactDamage * SwarmKindDamageMul(extra.kind)
                      * (dash ? g_ChargerDashDamageMul : 1.0);
            uint prev;
            counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE,
                                    SwarmHpToFixed(dmg), prev);
            // melee: from this enemy. A charger's dash knocks back like a blast
            RecordHit(dash ? 12u : 0u, enemies[i].position);
            cd = g_AttackInterval;
        }
    }

    enemies[i].attackCooldown = max(cd, 0.0);
}
