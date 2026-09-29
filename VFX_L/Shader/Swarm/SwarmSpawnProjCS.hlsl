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
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmProjectile> spawnRequests : register(t0);
StructuredBuffer<SwarmMotion> motions : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
Buffer<uint> enemyStates : register(t3);

RWStructuredBuffer<SwarmProjectile> projectiles : register(u0);
RWBuffer<uint> projStates : register(u1);
RWStructuredBuffer<SwarmProjPath> paths : register(u2);
RWByteAddressBuffer counters : register(u3);

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

    // ---- motion: split the request word, then build the path ----
    float sideSign = ((req.motion & SWARM_MOTION_FLIP_BIT) != 0u) ? -1.0 : 1.0;
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

    if (m.mode == SWARM_MOTION_DROP && speed > 0.01)
    {
        float3 fwd = float3(req.velocity.x, 0.0, req.velocity.z);
        float fwdLenSq = dot(fwd, fwd);
        fwd = (fwdLenSq > 1e-8) ? fwd * rsqrt(fwdLenSq) : float3(0, 0, 1);

        float3 impact = float3(req.position.x, g_PlayerPos.y, req.position.z) + fwd * 8.0;
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

        // start: up, and back toward the muzzle (comes in over the player's shoulder)
        float3 back = float3(req.position.x - impact.x, 0.0, req.position.z - impact.z);
        float backLenSq = dot(back, back);
        back = (backLenSq > 1e-8) ? back * rsqrt(backLenSq) : -fwd;
        float3 start = impact + back * m.c1.y + float3(0.0, max(m.c1.x, 1.0), 0.0);

        SwarmBuildDropPath(path, start, impact);
        req.position = start;
        req.velocity = normalize(impact - start) * (speed * 0.4);
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
            return;
        }
    }

    // pool full: drop it. same policy as the particle system's
    // deadCount guard -- degrade, never corrupt
}
