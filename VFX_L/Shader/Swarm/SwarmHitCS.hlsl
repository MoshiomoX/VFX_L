// ============================================================
// SwarmHitCS.hlsl
// Projectile vs enemy. One thread per projectile. Runs after ProjMoveCS
// in the fixed step, so positions are this step's final values.
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
// Which enemy (2026-10-25): it used to loop over every enemy slot in
// ascending order and take the first live one in reach. The result is
// the same, but the slots come from the spatial hash (SwarmEnemyBinCS,
// filled at the start of this step): the smallest candidate slot that
// passes the tests is hit; if it turns out a corpse, the smallest one
// after it, and so on. The query covers the coarse reach plus
// HIT_HASH_MOVE_MARGIN (enemies move between the binning and this pass).
// The fixed elites' slots (g_EliteCmd) are always tested as well (their
// teleport jumps any distance). A query that leaves the grid (enemies
// outside it are not binned), touches an overflowed bucket, covers too
// many cells, or runs without the hash bound scans every slot as before.
//
// Elites / the boss have a bigger capsule (SwarmSlotCapsule) and drop an
// orb worth SwarmKindExpMul times more. The kind is only read for enemies
// that pass a coarse test sized for the biggest body.
// The shield bearer takes SwarmArmorDamage; a shot whose motion has a
// freezeTime (Ice Lance) leaves a freeze request in animIndex (2026-10-08).
// The killer leaves the shot's element in the dead slot's attackCooldown
// (SwarmKillElementMark) for the kill number's colour (2026-10-25).
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
Buffer<uint> projTags : register(t5);  // trigger tag; the top bit = a lifesteal shot (SWARM_TAG_LIFESTEAL, 2026-10-25)
// spatial hash of the live enemies (SwarmEnemyBinCS, start of this step). Unbound = scan every slot
StructuredBuffer<uint> cellCount : register(t6);
StructuredBuffer<uint> cellItems : register(t7);

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
RWBuffer<uint> projStates : register(u0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u1);
RWBuffer<uint> enemyStates : register(u2);
RWByteAddressBuffer counters : register(u3);
RWStructuredBuffer<SwarmOrb> orbs : register(u4);
RWBuffer<uint> orbStates : register(u5);
static const uint HP_CORPSE_BIT = 0x80000000u;

// how far (m) an enemy may have moved since the bins were filled at the start of this step (AI velocity, the gust's
// push, the crowd push): anything under ~150 m/s at the 0.02 s step
static const float HIT_HASH_MOVE_MARGIN = 3.0;
// more cells than this in one query: the full scan is about as cheap
static const int HIT_HASH_MAX_CELLS = 256;
static const uint HIT_NONE = 0xFFFFFFFFu;

// The tests the old full scan ran on every slot, in the same order and with the same comparisons
// (a NaN position behaves as before). One exit (fxc warns X4000 on early returns)
bool CanHit(uint j, float3 ppos, float pradius, float coarseSq, uint lastHit)
{
    bool ok = (j < g_MaxEnemies) && (j != lastHit);
    if (ok)
        ok = (enemyStates[j] != SWARM_DEAD);
    float3 pos = float3(0, 0, 0);
    if (ok)
    {
        pos = enemies[j].position;
        float3 dc = ppos - pos;
        ok = !(dot(dc, dc) > coarseSq);
    }
    if (ok)
    {
        SwarmEnemyExtra extra = enemyExtra[j];
        ok = !SwarmUntargetable(extra);   // under the sand: cannot be hit (2026-10-17)
        if (ok)
        {
            float3 center;
            float er, eh;
            SwarmSlotCapsule(j, extra.kind, pos, center, er, eh);   // + the berserk elite's growth (2026-10-25)
            float3 d = ppos - center;
            d.y -= clamp(d.y, -eh, eh);
            float hitRadius = pradius + er;
            ok = !(dot(d, d) > hitRadius * hitRadius);
        }
    }
    return ok;
}

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
    // (+ the fixed elites, which may grow when berserk, 2026-10-25)
    float enraged = max(1.0, g_EliteEnragedScale);
    float kMax = max(max(1.0, max(g_EliteScale, g_BossScale)),
                     max(g_FixedEliteAScale, g_FixedEliteBScale) * enraged);
    float coarse = p.radius + kMax * 2.0 * (g_EnemyRadius + g_EnemyCapsuleHalf);
    float coarseSq = coarse * coarse;
    // Ice Lance (2026-10-08): the enemy it hits freezes for this long (AICS takes the request)
    float freezeTime = motions[p.motion & SWARM_MOTION_INDEX_MASK].freezeTime;
    // chain rune (2026-10-16): a chained shot survives a live hit while it has jumps left,
    // and never hits the enemy it jumped from again (see SwarmCommon.hlsli)
    uint state = projStates[i];
    bool chained = (state & SWARM_PROJ_CHAINED) != 0u;
    uint chainsLeft = chained ? ((state >> SWARM_PROJ_CHAIN_LEFT_SHIFT) & SWARM_PROJ_CHAIN_LEFT_MASK) : 0u;
    uint lastHit = chained ? (state & SWARM_PROJ_CHAIN_LAST_MASK) : SWARM_PROJ_CHAIN_NO_LAST;

    // ---- which cells to look in (see the header). Anything that can pass the tests is within `coarse` in xz ----
    uint hashCells, hashStride;
    cellCount.GetDimensions(hashCells, hashStride);
    float reach = coarse + HIT_HASH_MOVE_MARGIN;
    bool useHash = (hashCells >= g_GridW * g_GridD) && g_GridW > 0u && g_GridD > 0u && g_CellSize > 0.0
                && all(abs(p.position) < 1e20) && reach < 1e6;
    int x0 = 0, x1 = -1, z0 = 0, z1 = -1;
    if (useHash)
    {
        x0 = (int) floor((p.position.x - reach - g_GridOrigin.x) / g_CellSize);
        x1 = (int) floor((p.position.x + reach - g_GridOrigin.x) / g_CellSize);
        z0 = (int) floor((p.position.z - reach - g_GridOrigin.z) / g_CellSize);
        z1 = (int) floor((p.position.z + reach - g_GridOrigin.z) / g_CellSize);
        useHash = x0 >= 0 && z0 >= 0 && x1 < (int) g_GridW && z1 < (int) g_GridD
               && (x1 - x0 + 1) * (z1 - z0 + 1) <= HIT_HASH_MAX_CELLS;
    }
    if (useHash)
    {
        // a bucket that overflowed dropped some of its enemies: they would be missed
        [loop]
        for (int oz = z0; oz <= z1; ++oz)
        {
            [loop]
            for (int ox = x0; ox <= x1; ++ox)
            {
                if (cellCount[(uint) (oz * (int) g_GridW + ox)] > SWARM_BUCKET_CAP)
                    useHash = false;
            }
        }
    }

    uint after = 0u;   // the next candidate is at least this slot (ascending, like the old full scan)
    [loop]
    for (uint guard = 0u; guard <= g_MaxEnemies; ++guard)
    {
        // ---- the smallest slot >= after that can be hit ----
        uint j = HIT_NONE;
        if (useHash)
        {
            [loop]
            for (int cz = z0; cz <= z1; ++cz)
            {
                [loop]
                for (int cx = x0; cx <= x1; ++cx)
                {
                    uint cell = (uint) (cz * (int) g_GridW + cx);
                    uint n = min(cellCount[cell], SWARM_BUCKET_CAP);
                    [loop]
                    for (uint k = 0u; k < n; ++k)
                    {
                        // nested ifs: HLSL does not short-circuit &&, the tests should only run for a better slot
                        uint c = cellItems[cell * SWARM_BUCKET_CAP + k];
                        if (c >= after && c < j)
                        {
                            if (CanHit(c, p.position, p.radius, coarseSq, lastHit))
                                j = c;
                        }
                    }
                }
            }
            // the fixed elites may have teleported away from their bin this step
            [unroll]
            for (int r = 0; r < 4; ++r)
            {
                uint s1 = g_EliteCmd[r].x;
                if (s1 != 0u && s1 - 1u >= after && s1 - 1u < j)
                {
                    if (CanHit(s1 - 1u, p.position, p.radius, coarseSq, lastHit))
                        j = s1 - 1u;
                }
            }
        }
        else
        {
            [loop]
            for (uint c = after; c < g_MaxEnemies; ++c)
            {
                if (CanHit(c, p.position, p.radius, coarseSq, lastHit))
                {
                    j = c;
                    break;
                }
            }
        }
        if (j == HIT_NONE)
            return;

        SwarmEnemyExtra extra = enemyExtra[j];
        uint kind = extra.kind;
        // ---- hit: apply damage atomically (the shield bearer's armor first) ----
        uint dmgFixed = SwarmHpToFixed(SwarmTakenDamage(j, kind, p.damage));
        uint prev;
        InterlockedAdd(enemies[j].hp, (uint) (-(int) dmgFixed), prev);

        if (prev == 0u || prev >= HP_CORPSE_BIT)
        {
            after = j + 1u;
            continue; // corpse. someone else already killed it this step
        }

        // ---- hit stun: freeze + flash (MoveCS / AICS / VS read it) ----
        // plain stores: several hits in one step all write the same value.
        // harmless on the killer, its slot goes DEAD right below.
        // The boss never flinches (it would be pinned by the constant fire), nor does a
        // charger that has started its wind-up (MoveCS would stop the dash on every hit).
        // An ice shot leaves a freeze request instead of the stun
        bool noFlinch = SwarmNoFlinch(extra);   // (+ both final-swarm ghosts, 2026-10-18)
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

        if (prev > dmgFixed)
        {
            // not the killer: remember what hit it (the damage number's colour). CAS so a DEAD written meanwhile
            // by another thread's killing hit is never turned back into ALIVE (2026-10-17)
            uint cur = enemyStates[j];
            if (cur != SWARM_DEAD)
            {
                uint wasState;
                // + the lifesteal mark: SwarmDamageTextCS counts this enemy's hp loss toward the player's heal (2026-10-25)
                const uint steal = ((projTags[i] & SWARM_TAG_LIFESTEAL) != 0u) ? SWARM_STATE_LIFESTEAL : 0u;
                InterlockedCompareExchange(enemyStates[j], cur,
                    SWARM_ALIVE | (SwarmMotionElement(motions[p.motion & SWARM_MOTION_INDEX_MASK]) << SWARM_STATE_ELEMENT_SHIFT) | steal, wasState);
            }
        }

        if (prev <= dmgFixed)
        {
            // ---- this hit was the killer ----
            enemyStates[j] = SWARM_DEAD;
            // the kill number's colour = this shot's element (the state word is DEAD now. 2026-10-25)
            enemies[j].attackCooldown = SwarmKillElementMark(SwarmMotionElement(motions[p.motion & SWARM_MOTION_INDEX_MASK]));
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
        SwarmSpawnAreaFromDefX(motions[p.motion & SWARM_MOTION_INDEX_MASK].hitArea,
                               p.position, i + j, SwarmProjScale(p), projBoost[i], p.velocity.xz,
                               ((projTags[i] & SWARM_TAG_LIFESTEAL) != 0u) ? SWARM_AREA_EXTRA_LIFESTEAL : 0u);

        // projectile is consumed either way (no pierce) -- unless it still has chain jumps:
        // then it stays alive, remembers this enemy and asks ProjMoveCS for the next one
        if (chainsLeft > 0u)
            projStates[i] = SwarmProjChainState(chainsLeft - 1u, j) | SWARM_PROJ_CHAIN_PENDING;
        else
            projStates[i] = SWARM_DEAD;
        return;
    }
}
