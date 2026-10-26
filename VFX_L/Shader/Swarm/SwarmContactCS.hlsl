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
// area is left behind only for the GPU explosion VFX. Once lit, a bomber
// (and a dash bomber) cannot be hurt (SwarmInvulnerable, 2026-10-18): it
// can only be killed (normal kill + orb) before it lights.
//
// Every hit also records where it came from in playerHits (u6) so the
// CPU can knock the player back (melee a little, blasts ~3x + a hop).
//
// A frozen enemy (enemySlow.z > 0, Ice Lance) does not hit or light its fuse.
// A dashing charger hits g_ChargerDashDamageMul harder and knocks the player
// back like a blast (2026-10-08).
//
// Only writer of a live enemy's Enemy.attackCooldown (the killers in HitCS /
// AreaDamageCS leave the kill element in a dead slot's, SwarmKillElementMark),
// of the plain bomber's SwarmEnemyExtra.fuse, and of the dash bomber's when it
// blows up (resets it to 0). Every other kind's fuse belongs to AICS.
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
// Swarm::PlayerHitInfo: melee x, z, count, blast x, z, count, spike x, z, count, wind x, z, count (2026-10-17).
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

// the blast of a bomber / dash bomber: the player within `radius` takes `dmg` and a blast knockback, the slot goes DEAD
// (no kill, no orb), the look is the BomberBlast area scaled by `scale` (its own radius is the bomber's)
void Explode(uint i, float3 pos, float radius, float dmg, float scale)
{
    if (g_PlayerAlive != 0u && PlayerWithin(pos, radius + g_PlayerRadius))
    {
        uint prev;
        counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE, SwarmHpToFixed(dmg), prev);
        RecordHit(12u, pos);   // blast: from the bomber
    }
    enemyStates[i] = SWARM_DEAD;
    enemyExtra[i].fuse = 0.0;
    SwarmSpawnAreaFromDef(g_BomberBlastArea, pos, i, scale, 0u, float2(0.0, 0.0));
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

    Explode(i, pos, g_BomberBlastRadius, g_BomberBlastDamage, 1.0);
}

// dash bomber (2026-10-18): AICS lights it, holds it g_DashBomberWindup seconds, then sprints it along its yaw
// (fuse counts up there); here it blows up the step it touches the player, or g_DashBomberFuse seconds after
// the wind-up (it ran past). Never melees
void UpdateDashBomber(uint i, SwarmEnemyExtra extra)
{
    if (extra.fuse <= 0.0)
        return;
    float3 pos = enemies[i].position;
    float reach = g_EnemyRadius * g_DashBomberScale + g_PlayerRadius + CONTACT_SKIN;
    bool touch = g_PlayerAlive != 0u && PlayerWithinCapsule(pos, g_EnemyCapsuleHalf * g_DashBomberScale, reach);
    if (touch || extra.fuse >= g_DashBomberWindup + g_DashBomberFuse)
        Explode(i, pos, g_DashBomberBlastRadius, g_BomberBlastDamage * g_DashBomberDamageMul,
                g_DashBomberBlastRadius / max(g_BomberBlastRadius, 0.1));
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
    if (extra.kind == SWARM_KIND_DASHBOMBER)
    {
        UpdateDashBomber(i, extra);   // (2026-10-18) a lit one cannot be frozen (AICS); an unlit one never melees
        return;
    }
    if (extra.kind == SWARM_KIND_BOSS && (SwarmBuried(extra) || g_BossNoContact > 0.5))
        return;   // the sand guardian under the sand / the werewolf mid-pounce does not touch (the move's own hit is judged by the CPU)
    // a fixed elite in the middle of a move / vanished does not melee: the move's own hit is judged by the CPU (2026-10-25)
    if (SwarmIsFixedElite(extra.kind) && (SwarmEliteFlags(i) & SWARM_ELITE_NO_CONTACT) != 0u)
    {
        enemies[i].attackCooldown = max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_BURROWER)
    {
        // burrower: never bites by touch. One bite when it surfaces: g_BurrowBiteDelay seconds after
        // coming out (fuse counts down from g_BurrowSurfaceTime, this is the step it crosses the
        // threshold) everyone within g_BurrowBiteRadius takes a hit, then it digs back in (2026-10-17)
        float f = extra.fuse;
        float thr = g_BurrowSurfaceTime - g_BurrowBiteDelay;
        if (f > 0.0 && f <= thr && f + g_Step > thr && g_PlayerAlive != 0u && !frozen)
        {
            float3 center;
            float er, eh;
            SwarmKindCapsule(extra.kind, enemies[i].position, center, er, eh);
            if (PlayerWithinCapsule(center, eh, g_BurrowBiteRadius + g_PlayerRadius))
            {
                float dmg = g_ContactDamage * SwarmKindDamageMul(extra.kind);
                uint prev;
                counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE, SwarmHpToFixed(dmg), prev);
                RecordHit(0u, enemies[i].position);   // melee-sized knockback
                enemies[i].attackCooldown = g_AttackInterval;   // the VS plays the attack swing from this
            }
        }
        else
            enemies[i].attackCooldown = max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_SANDMAGE || extra.kind == SWARM_KIND_NECRO)
    {
        // sand mage / necromancer: never melee. The attack swing plays while winding up (fuse > 0, AICS) (2026-10-17 / 10-25)
        enemies[i].attackCooldown = (extra.fuse > 0.0) ? g_AttackInterval : max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_WARDEN && extra.fuse > 0.0)
    {
        // chain warden winding up the throw: swings (the hook in its hand), no melee (2026-10-25)
        enemies[i].attackCooldown = g_AttackInterval;
        return;
    }
    // stone gargoyle (2026-10-25): no touch while asleep / waking / flying (it is up in the air). The dive hits
    // like the charger's dash (below); landed it melees as usual
    bool gargDive = SwarmGargoyleDiving(extra);
    if (extra.kind == SWARM_KIND_GARGOYLE && !gargDive && !SwarmGargoyleLanded(extra))
    {
        enemies[i].attackCooldown = max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_TORNADO)
    {
        // tornado: expired (AICS set fuse < 0) -> the slot dies here, no kill, no orb (the player did not kill it).
        // Touching the player: contact damage x g_TornadoDamageMul and the "wind" knockback (slot 36: high up).
        // Own cooldown so one tornado does not hit every step while the player is inside it (2026-10-17)
        if (extra.fuse < 0.0)
        {
            enemyStates[i] = SWARM_DEAD;
            return;
        }
        float cd = enemies[i].attackCooldown - g_Step;
        if (cd <= 0.0 && g_PlayerAlive != 0u)
        {
            float3 center;
            float er, eh;
            SwarmKindCapsule(extra.kind, enemies[i].position, center, er, eh);
            if (PlayerWithinCapsule(center, eh, er + g_PlayerRadius + CONTACT_SKIN))
            {
                float dmg = g_ContactDamage * SwarmKindDamageMul(extra.kind);
                uint prev;
                counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE, SwarmHpToFixed(dmg), prev);
                RecordHit(36u, enemies[i].position);
                cd = g_AttackInterval;
            }
        }
        enemies[i].attackCooldown = max(cd, 0.0);
        return;
    }

    float cd = enemies[i].attackCooldown - g_Step;

    if (cd <= 0.0 && g_PlayerAlive != 0u && !frozen)
    {
        // elites / boss: bigger capsule, harder hits (a berserk fixed elite grows, 2026-10-25)
        float3 center;
        float er, eh;
        SwarmSlotCapsule(i, extra.kind, enemies[i].position, center, er, eh);
        float reach = er + g_PlayerRadius + CONTACT_SKIN;
        if (PlayerWithinCapsule(center, eh, reach))
        {
            bool charge = (extra.kind == SWARM_KIND_CHARGER)
                     && (SwarmChargerPhase(extra.fuse) == SWARM_CHARGE_DASH);
            bool dash = charge || gargDive;   // the gargoyle's dive hits like the charger's dash (2026-10-25)
            float dmg = g_ContactDamage * SwarmKindDamageMul(extra.kind)
                      * (charge ? g_ChargerDashDamageMul : gargDive ? g_GargoyleDiveDamageMul : 1.0);
            uint prev;
            counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE,
                                    SwarmHpToFixed(dmg), prev);
            // melee: from this enemy. A charger's dash knocks back like a blast; a spike ball throws
            // the player (its own slot at 24: the CPU knocks back farther and higher) (2026-10-17).
            // Both final-swarm ghosts hit like a blast too (2026-10-18, Megabonk's ghosts slap the player across the map)
            uint slotBase = (extra.kind == SWARM_KIND_SPIKEBALL) ? 24u : ((dash || SwarmIsGhost(extra.kind)) ? 12u : 0u);
            RecordHit(slotBase, enemies[i].position);
            cd = g_AttackInterval;
        }
    }

    enemies[i].attackCooldown = max(cd, 0.0);
}
