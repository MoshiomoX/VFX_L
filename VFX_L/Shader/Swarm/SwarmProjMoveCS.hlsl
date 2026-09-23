// ============================================================
// SwarmProjMoveCS.hlsl
// One fixed step of projectile integration.
//
// g_Step is always the same value here -- that is the point of the
// fixed step. The result cannot depend on frame rate.
//
// Three motion modes (SwarmMotion.mode):
//   STRAIGHT   : position += velocity * step. Never looks at enemies.
//   CURVE_ONCE : follows the Bezier built at spawn toward the locked
//                enemy. The end point tracks that enemy while it lives.
//                Enemy dies / curve ends -> keeps flying straight along
//                the current velocity. Never locks again.
//   TRACK      : same curve, but when the target is gone it searches
//                the enemy nearest to ITSELF and builds a new curve
//                from where it is, leaving along its current heading.
//
// velocity always holds the real velocity of this step (curve or not):
// EmitCS sweeps particles back along it and the trail reads it.
// ============================================================
// a projectile that expires / hits a wall may leave an area behind
#define SWARM_AREA_POOL_U u4
#define SWARM_AREA_STATE_U u5
#define SWARM_AREA_DEF_T t4
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<uint> terrain : register(t0);
StructuredBuffer<SwarmMotion> motions : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
Buffer<uint> enemyStates : register(t3);
StructuredBuffer<float> terrainHeight : register(t5);   // ramps (t4 = area defs)

RWStructuredBuffer<SwarmProjectile> projectiles : register(u0);
RWBuffer<uint> projStates : register(u1);
RWByteAddressBuffer counters : register(u2);
RWStructuredBuffer<SwarmProjPath> paths : register(u3);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;

    if (projStates[i] == SWARM_DEAD)
        return;

    SwarmProjectile p = projectiles[i];

    SwarmMotion m = motions[p.motion & SWARM_MOTION_INDEX_MASK];
    bool areaOnExpire = (m.hitArea != 0u) && ((m.hitAreaFlags & SWARM_HITAREA_ON_EXPIRE) != 0u);

    // ---- lifetime ----
    p.lifetime -= g_Step;
    if (p.lifetime <= 0.0)
    {
        if (areaOnExpire)
            SwarmSpawnAreaFromDef(m.hitArea, p.position, i);
        projStates[i] = SWARM_DEAD;
        return;
    }

    bool onCurve = false;

    if (m.mode != SWARM_MOTION_STRAIGHT)
    {
        SwarmProjPath path = paths[i];
        bool pathDirty = false;

        // ---- target lost? ----
        if (path.target != SWARM_NO_TARGET)
        {
            if (path.target >= g_MaxEnemies || enemyStates[path.target] != SWARM_ALIVE)
            {
                path.target = SWARM_NO_TARGET;
                pathDirty = true;

                // keep the heading, go back to the nominal speed
                float lostLenSq = dot(p.velocity, p.velocity);
                if (lostLenSq > 1e-8)
                    p.velocity *= rsqrt(lostLenSq) * path.speed;
            }
        }

        // ---- TRACK: look for a new target, nearest to the projectile ----
        if (path.target == SWARM_NO_TARGET && m.mode == SWARM_MOTION_TRACK)
        {
            float bestSq = (m.retargetRadius > 0.0)
                         ? m.retargetRadius * m.retargetRadius : 1e30;
            uint best = SWARM_NO_TARGET;

            for (uint j = 0; j < g_MaxEnemies; ++j)
            {
                if (enemyStates[j] != SWARM_ALIVE)
                    continue;
                float3 d = enemies[j].position - p.position;
                float dSq = dot(d, d);
                if (dSq < bestSq)
                {
                    bestSq = dSq;
                    best = j;
                }
            }

            if (best != SWARM_NO_TARGET)
            {
                float vLenSq = dot(p.velocity, p.velocity);
                float3 heading = (vLenSq > 1e-8) ? p.velocity * rsqrt(vLenSq) : float3(0, 0, 1);

                path.target = best;
                SwarmBuildPath(path, m, p.position, enemies[best].position, heading, true);
                p.pathT = 0.0;
                pathDirty = true;
            }
        }

        // ---- follow the curve ----
        if (path.target != SWARM_NO_TARGET)
        {
            // the end point rides on the target; drag the second handle
            // along so the arrival shape survives the target moving
            float3 newEnd = enemies[path.target].position;
            path.p2 += newEnd - path.p3;
            path.p3 = newEnd;

            p.pathT += g_Step / path.duration;
            float t = min(p.pathT, 1.0);

            float3 newPos = SwarmBezier(path, t);
            p.velocity = (newPos - p.position) / g_Step;
            p.position = newPos;
            onCurve = true;
            pathDirty = true;

            // end of the curve without a hit: let go and fly straight at the
            // nominal speed. TRACK will search again next step
            if (p.pathT >= 1.0)
            {
                float3 tan1 = SwarmBezierTangent(path, 1.0);
                float tanLenSq = dot(tan1, tan1);
                if (tanLenSq > 1e-8)
                    p.velocity = tan1 * (rsqrt(tanLenSq) * path.speed);
                path.target = SWARM_NO_TARGET;
            }
        }

        if (pathDirty)
            paths[i] = path;
    }

    // ---- straight flight ----
    // no gravity: projectiles fly straight (same as the CPU version)
    if (!onCurve)
        p.position += p.velocity * g_Step;

    // ---- terrain ----
    // entering a blocked cell kills it. grid precision is 2m but a
    // wall is 2m thick anyway, so nothing slips through.
    // ramps are walkable cells with a height: flying below the
    // surface is a hit too (the projectile would be inside the block)
    if (!SwarmIsWalkable(terrain, p.position)
        || p.position.y < SwarmTerrainHeight(terrainHeight, p.position.xz))
    {
        // blow up at the last free position, not inside the wall
        if (areaOnExpire)
            SwarmSpawnAreaFromDef(m.hitArea, projectiles[i].position, i);
        projStates[i] = SWARM_DEAD;
        return;
    }

    projectiles[i].position = p.position;
    projectiles[i].velocity = p.velocity;
    projectiles[i].lifetime = p.lifetime;
    projectiles[i].pathT = p.pathT;
    uint prevCount;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_PROJ, 1u, prevCount);
}
