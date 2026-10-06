// ============================================================
// SwarmSpawnCS.hlsl
// Push pending spawn requests into free slots of the pool.
//
// One thread per request. Each thread scans for a dead slot and
// claims it with InterlockedCompareExchange on the state buffer.
//
// Why a linear scan instead of a free list:
//   a consume buffer needs CopyStructureCount every frame, which
//   flushes the command queue (~0.076ms measured in the particle
//   system). Requests per frame are few, so scanning is cheaper.
//
// The scan starts at a per-thread offset so threads do not all
// fight over slot 0.
//
// Motion: the request carries a motion table index (+ a mirror flip
// bit). For the curved modes the flight path is built HERE, once,
// from the muzzle to the enemy nearest to the player -- the same
// enemy the weapon aimed at (counters still hold last step's key).
// No target -> the projectile simply flies straight.
//
// DROP (meteor): the same enemy fixes the impact point. The projectile
// is moved up to its start point in the sky (c1, see SwarmCommon.hlsli)
// and gets enough lifetime to reach the ground. No target -> it lands
// 8m ahead of the muzzle at the player's height.
// With SWARM_SPAWN_AT_POS (a triggered meteor) the request position IS
// the impact point (where the fireball / stone shot ended) and it comes
// in over the player's side.
// Inside the mine cave (2026-10-03: the ground at the impact is below the
// plain, and the mine is roofed): fall from right above the impact, under
// the roof. Coming in over the player's side could start outside the cave
// and fly through the rock mass.
//
// Every claimed slot gets its trigger tag (0 = none) in projTags, so a
// reused slot never keeps an old tag. Same for projBoost (the damage /
// duration multipliers of the areas it leaves, see SwarmBoostDamage).
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmProjectile> spawnRequests : register(t0);
StructuredBuffer<SwarmMotion> motions : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
Buffer<uint> enemyStates : register(t3);
StructuredBuffer<uint2> spawnExtra : register(t4);   // x = trigger tag, y = SWARM_SPAWN_* flags
StructuredBuffer<float> terrainHeight : register(t5); // GridWorld::Heights (the mine cave test)

// ground lower than this (m) = the mine floor, under the roof
static const float kCaveFloorBelow = -1.0;
// fall height / back offset there (roof underside is ~15m above the floor)
static const float kCaveDropHeight = 8.0;
static const float kCaveDropBack = 2.0;

RWStructuredBuffer<SwarmProjectile> projectiles : register(u0);
RWBuffer<uint> projStates : register(u1);
RWStructuredBuffer<SwarmProjPath> paths : register(u2);
RWByteAddressBuffer counters : register(u3);
RWBuffer<uint> projTags : register(u4);
RWBuffer<uint> projBoost : register(u5);   // y >> SWARM_SPAWN_BOOST_SHIFT: damage / duration boost of the areas it leaves

cbuffer SwarmSpawnCB : register(b1)
{
    uint g_RequestCount;
    uint g_ScanStart;
    uint2 _spawnPad;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_RequestCount)
        return;

    SwarmProjectile req = spawnRequests[id.x];
    uint2 extra = spawnExtra[id.x];

    // ---- motion: split the request word, then build the path ----
    // sideSign is the roll of the curve's side offset around the flight axis (radians):
    // 0 = right, pi = left (the flip bit), anything else = a random direction per shot
    // (ProjectileProfile Mirror::RandomAngle, 7 bits in extra.y: 0..127 = 0..360 deg. 2026-10-06)
    float sideSign = ((req.motion & SWARM_MOTION_FLIP_BIT) != 0u) ? 3.14159265 : 0.0;
    sideSign += (float)((extra.y >> 1) & 0x7Fu) * (6.28318531 / 128.0);
    uint motionIdx = req.motion & SWARM_MOTION_INDEX_MASK;
    req.pathT = 0.0;

    SwarmMotion m = motions[motionIdx];

    // size scale (the Magnifier item): this shot's hit radius against the
    // profile's. Kept in the motion word so every shader after this sees it
    float sizeScale = (m.baseRadius > 0.0) ? req.radius / m.baseRadius : 1.0;
    req.motion = motionIdx | (SwarmPackScale(sizeScale, 0x7FFFu) << SWARM_SCALE_SHIFT);

    float speed = length(req.velocity);

    SwarmProjPath path = (SwarmProjPath) 0;
    path.target = SWARM_NO_TARGET;
    path.sideSign = sideSign;
    path.speed = speed;
    path.duration = 1.0;

    if (SwarmMotionLands(m.mode) && speed > 0.01)
    {
        float3 fwd = float3(req.velocity.x, 0.0, req.velocity.z);
        float fwdLenSq = dot(fwd, fwd);
        fwd = (fwdLenSq > 1e-8) ? fwd * rsqrt(fwdLenSq) : float3(0, 0, 1);

        float3 impact = float3(req.position.x, g_PlayerPos.y, req.position.z) + fwd * 8.0;
        // start: up, and back toward the muzzle (comes in over the player's shoulder)
        float3 backFrom = req.position;
        if ((extra.y & SWARM_SPAWN_AT_POS) != 0u)
        {
            // triggered: the CPU already chose the impact point
            impact = req.position;
            backFrom = g_PlayerPos;
        }
        else
        {
            uint key = counters.Load(SWARM_CNT_NEAREST_KEY);
            if (key != SWARM_NO_TARGET_KEY)
            {
                uint slot = key & SWARM_SLOT_MASK;
                if (slot < g_MaxEnemies && enemyStates[slot] == SWARM_ALIVE)
                {
                    impact = enemies[slot].position;
                    path.target = slot;
                }
            }
        }

        if (m.mode == SWARM_MOTION_LOB)
        {
            // thrown from the muzzle (from the player when the CPU chose the spot)
            float3 from = ((extra.y & SWARM_SPAWN_AT_POS) != 0u) ? g_PlayerPos : req.position;
            SwarmBuildPath(path, m, from, impact, fwd, false);
            float3 tan0 = SwarmBezierTangent(path, 0.0);
            float tanLenSq = dot(tan0, tan0);
            req.position = from;
            req.velocity = (tanLenSq > 1e-8) ? tan0 * (rsqrt(tanLenSq) * speed) : fwd * speed;
        }
        else
        {
            float3 back = float3(backFrom.x - impact.x, 0.0, backFrom.z - impact.z);
            float backLenSq = dot(back, back);
            back = (backLenSq > 1e-8) ? back * rsqrt(backLenSq) : -fwd;
            bool inCave = SwarmTerrainHeight(terrainHeight, impact.xz) < kCaveFloorBelow;
            float backDist = inCave ? min(m.c1.y, kCaveDropBack) : m.c1.y;
            float upDist = inCave ? min(max(m.c1.x, 1.0), kCaveDropHeight) : max(m.c1.x, 1.0);
            float3 start = impact + back * backDist + float3(0.0, upDist, 0.0);

            SwarmBuildDropPath(path, start, impact);
            req.position = start;
            req.velocity = normalize(impact - start) * (speed * 0.4);
        }
        req.lifetime = max(req.lifetime, path.duration + 0.25);
    }
    else if (m.mode != SWARM_MOTION_STRAIGHT && speed > 0.01)
    {
        uint key = counters.Load(SWARM_CNT_NEAREST_KEY);
        if (key != SWARM_NO_TARGET_KEY)
        {
            uint slot = key & SWARM_SLOT_MASK;
            if (slot < g_MaxEnemies && enemyStates[slot] == SWARM_ALIVE)
            {
                path.target = slot;
                SwarmBuildPath(path, m, req.position, enemies[slot].position,
                               req.velocity / speed, false);

                // leave the muzzle along the curve, not along the aim line
                float3 tan0 = SwarmBezierTangent(path, 0.0);
                float tanLenSq = dot(tan0, tan0);
                if (tanLenSq > 1e-8)
                    req.velocity = tan0 * (rsqrt(tanLenSq) * speed);
            }
        }
    }

    // spread starting points so threads claim different regions
    uint start = (g_ScanStart + id.x * 97u) % g_MaxProjectiles;

    for (uint i = 0; i < g_MaxProjectiles; ++i)
    {
        uint slot = (start + i) % g_MaxProjectiles;

        uint prev;
        InterlockedCompareExchange(projStates[slot],
                                   SWARM_DEAD, SWARM_ALIVE, prev);

        if (prev == SWARM_DEAD)
        {
            // won the slot. state is already ALIVE from the exchange
            projectiles[slot] = req;
            paths[slot] = path;
            projTags[slot] = extra.x;
            projBoost[slot] = extra.y >> SWARM_SPAWN_BOOST_SHIFT;
            return;
        }
    }

    // pool full: drop it. same policy as the particle system's
    // deadCount guard -- degrade, never corrupt
}
