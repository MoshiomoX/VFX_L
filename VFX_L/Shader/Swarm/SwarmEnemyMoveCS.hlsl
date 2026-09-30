// ============================================================
// SwarmEnemyMoveCS.hlsl
// One fixed step of enemy integration. Runs after SwarmEnemyAICS
// so every velocity is final before any position moves.
//
// No gravity, no falling: y is pinned to g_GroundY + terrain height.
// The height field (GridWorld::Heights, SWARM_HEIGHT_SUB cells per grid
// cell) is 0 on flat ground and the walkable surface height on ramps
// (trapezoid blocks), so enemies climb slopes by following it. Boxes
// and walls stay 0 there and block through the walkable grid instead.
// yaw is stored in radians (the VS does sin/cos on it directly).
//
// Also counts alive enemies for the readback.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<float> terrainHeight : register(t1);
StructuredBuffer<uint> terrain : register(t2); // walkable grid (for the slide / walk-out)
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3); // kind: ghosts skip the walls
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer counters : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    SwarmEnemy e = enemies[i];

    if (e.animIndex == 2u)
    {
        // ---- hit stun: hold position and facing, only the clock runs ----
        // HitCS set animTime = 0 when it landed the hit
        e.animTime += g_Step;
        if (e.animTime >= g_HitStun)
        {
            e.animIndex = 0u;
            e.animTime = 0.0;
        }
        e.position.y = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz);
    }
    else
    {
        // ---- integrate ----
        // The AI's hard block is a look-ahead on cell granularity and the
        // player push / separation can still carry a velocity into a wall, so
        // the step itself is checked here (2026-09-30, enemies used to end up
        // inside trees and walls): a step that ends in a blocked cell or over
        // a cliff is reduced to the axis that stays free (slide along the
        // wall), and dropped if neither does.
        // An enemy that is already inside a blocked cell (pushed in, spawned
        // in) is walked toward the nearest free cell instead, ignoring the
        // walls on the way (they are what it is escaping from).
        float2 from = e.position.xz;
        if (enemyExtra[i].kind == SWARM_KIND_GHOST)
        {
            // ghost: flies straight, through walls and over plateaus (y still follows the ground below)
            e.position.xz = from + e.velocity.xz * g_Step;
        }
        else if (!SwarmIsWalkable(terrain, e.position))
        {
            float2 best = from;
            float bestD = 1e30;
            int gx = (int) floor((from.x - g_GridOrigin.x) / g_CellSize);
            int gz = (int) floor((from.y - g_GridOrigin.z) / g_CellSize);
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                {
                    int cx = gx + dx, cz = gz + dz;
                    if (cx < 0 || cx >= (int) g_GridW || cz < 0 || cz >= (int) g_GridD)
                        continue;
                    if (terrain[cz * g_GridW + cx] == 0u)
                        continue;
                    float2 c = float2(g_GridOrigin.x + (cx + 0.5) * g_CellSize,
                                      g_GridOrigin.z + (cz + 0.5) * g_CellSize);
                    float d = dot(c - from, c - from);
                    if (d < bestD) { bestD = d; best = c; }
                }
            if (bestD < 1e29)
            {
                float2 dir = best - from;
                float len = length(dir);
                float stepLen = min(len, max(e.moveSpeed, 2.0) * g_Step);
                if (len > 1e-4)
                    e.position.xz = from + dir / len * stepLen;
            }
        }
        else
        {
            float2 next = from + e.velocity.xz * g_Step;
            float3 n3 = float3(next.x, 0.0, next.y);
            if (SwarmIsWalkable(terrain, n3) && SwarmSlopeOk(terrainHeight, from, next))
                e.position.xz = next;
            else
            {
                float2 nx = float2(next.x, from.y);
                float2 nz = float2(from.x, next.y);
                if (SwarmIsWalkable(terrain, float3(nx.x, 0.0, nx.y)) && SwarmSlopeOk(terrainHeight, from, nx))
                    e.position.xz = nx;
                else if (SwarmIsWalkable(terrain, float3(nz.x, 0.0, nz.y)) && SwarmSlopeOk(terrainHeight, from, nz))
                    e.position.xz = nz;
            }
        }
        e.position.y = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz);

        // ---- facing: turn toward the velocity at a bounded rate ----
        if (dot(e.velocity.xz, e.velocity.xz) > 0.01)
        {
            float targetYaw = atan2(e.velocity.x, e.velocity.z);
            float delta = targetYaw - e.yaw;
            delta = atan2(sin(delta), cos(delta)); // wrap to [-PI, PI]
            float maxTurn = g_TurnSpeed * g_Step;
            e.yaw += clamp(delta, -maxTurn, maxTurn);
        }
        // ---- animation clock (nobody reads it yet) ----
        e.animTime += g_Step;
    }

    enemies[i].position = e.position;
    enemies[i].yaw = e.yaw;
    enemies[i].animTime = e.animTime;
    enemies[i].animIndex = e.animIndex;

    uint prev;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_ENEMIES, 1u, prev);
     // ---- aim: compete for "closest to the player" ----
    // XZ distance only, same as everything else in this pipeline
    float3 dp = e.position - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);
    uint key = (asuint(dist) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK);
    counters.InterlockedMin(SWARM_CNT_NEAREST_KEY, key, prev);
}