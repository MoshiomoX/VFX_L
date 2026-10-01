// ============================================================
// SwarmEnemyMoveCS.hlsl
// One fixed step of enemy integration. Runs after SwarmEnemyAICS
// so every velocity is final before any position moves.
//
// y sticks to g_GroundY + terrain height, except after stepping off a
// cliff (allowed toward a player lower down, 2026-10-01): then the enemy
// falls with kFallGravity (velocity.y, written only here) until it lands.
// The height field (GridWorld::Heights, SWARM_HEIGHT_SUB cells per grid
// cell) is 0 on flat ground and the walkable surface height on ramps
// (trapezoid blocks), so enemies climb slopes by following it. Boxes
// and walls stay 0 there and block through the walkable grid instead.
// yaw is stored in radians (the VS does sin/cos on it directly).
//
// Also counts alive enemies for the readback.
// ============================================================
#define SWARM_BOMBER_CB_REG b3   // kind body scale (elites / boss keep a bigger gap to walls)
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<float> terrainHeight : register(t1);
StructuredBuffer<uint> terrain : register(t2); // walkable grid (for the slide / walk-out)
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3); // kind: ghosts skip the walls
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer counters : register(u1);

// m/s a body that touches a wall / cliff face is eased back out
static const float kWallPushOut = 2.0;
static const float kWallPushOutFalling = 4.0;

// falling off a plateau (2026-10-01): m/s^2, same as the battle scene's gravity
static const float kFallGravity = 25.0;
// the ground may drop this much in one step and the enemy still sticks to it
// (walking down a ramp drops ~0.05m a step; stepping off a cliff edge drops
// ~0.5m a step through the bilinear blur, so that one starts a fall)
static const float kSnapDown = 0.25;

// one step from -> to: the centre lands on a walkable cell without a
// cliff on the way, and (bodyCheck) the body circle there touches nothing.
// easing out of a face (!bodyCheck) the cliff test uses the raw heights
// (SwarmRawSlopeOk: the bilinear one would not let it step away).
// allowDrop (player lower down): a cliff going down passes and lower ground
// under the body does not count, so the enemy walks off the edge.
// footH = the feet height (terrain space) the body's rises are measured from
bool StepOk(float2 from, float2 to, float radius, bool bodyCheck, bool allowDrop, float footH)
{
    bool ok = SwarmIsWalkable(terrain, float3(to.x, 0.0, to.y));
    if (ok)
        ok = bodyCheck ? SwarmStepHeightOk(terrainHeight, from, to, allowDrop) : SwarmRawSlopeOk(terrainHeight, from, to);
    if (ok && bodyCheck)
        ok = (SwarmBodyContact(terrain, terrainHeight, to, radius, !allowDrop, footH).z == 0.0);
    return ok;
}

// a cliff under the body circle: the highest and lowest raw height cell differ by this much
static const float kEdgeCliff = 0.8;

// y: stick to the ground, or fall to it (velocity.y = fall speed, only this pass writes it).
// Dropping off an edge (allowDrop): the highest ground under the body circle holds the
// enemy up, so it neither sinks into the bilinear blur at the plateau edge nor starts
// falling while half its body is still over the plateau (the face used to cut through
// it on the way down). It falls once the whole circle is past the edge
void Settle(inout SwarmEnemy e, float radius, bool allowDrop)
{
    float ground = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz);
    if (allowDrop)
    {
        float feet = e.position.y - g_GroundY;
        float hi = SwarmTerrainHeightRaw(terrainHeight, e.position.xz), lo = hi;
        [unroll]
        for (int k = 0; k < 8; ++k)
        {
            float a = (float) k * 0.7853982;
            float h = SwarmTerrainHeightRaw(terrainHeight, e.position.xz + float2(cos(a), sin(a)) * radius);
            hi = max(hi, h);
            lo = min(lo, h);
        }
        // still standing on the edge (feet level with the high side), not a body
        // at the cliff foot that only touches the face
        if (hi - lo > kEdgeCliff && feet >= hi - kSnapDown)
            ground = max(ground, g_GroundY + hi);
    }
    if (e.position.y > ground + kSnapDown)
    {
        e.velocity.y -= kFallGravity * g_Step;
        e.position.y = max(e.position.y + e.velocity.y * g_Step, ground);
        if (e.position.y <= ground)
            e.velocity.y = 0.0;   // landed
    }
    else
    {
        e.position.y = ground;
        e.velocity.y = 0.0;
    }
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    SwarmEnemy e = enemies[i];
    // body radius against walls, and whether cliffs going down are open (player lower down)
    float radius = SwarmBodyWallRadius(enemyExtra[i].kind);
    bool allowDrop = SwarmDropAllowed(terrainHeight, e.position.xz);

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
        Settle(e, radius, allowDrop);   // hit in mid-air: keeps falling
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
            // The body circle has to stay clear of walls and cliff faces too, not
            // just the centre (2026-10-01, see SwarmBodyContact). A body that
            // already touches one (pushed in by the crowd, spawned there) eases
            // out at kWallPushOut while keeping the part of its velocity that
            // runs along the wall. When the touches cancel out (squeezed from
            // both sides) only the centre is checked, as before.
            // rises are measured from the feet: on the ground = the raw ground under the centre (as
            // before); stepping off / falling = the real feet (the plateau behind is no wall until
            // the feet sink below it, then it eases the body away from the face on the way down)
            // Only while dropping is allowed: at a cliff foot the bilinear ground lifts the feet
            // too, and with drops counted the low side would then touch as well, cancel the
            // push-out and pin the body in the face (self test clip / climb)
            float rawHere = SwarmTerrainHeightRaw(terrainHeight, from);
            float feet = e.position.y - g_GroundY;
            float footH = allowDrop ? max(rawHere, feet) : rawHere;
            bool airborne = (feet > SwarmTerrainHeight(terrainHeight, from) + kSnapDown);   // falling (Settle)
            float3 touch = SwarmBodyContact(terrain, terrainHeight, from, radius, !allowDrop, footH);
            bool easeOut = (touch.z > 0.0 && dot(touch.xy, touch.xy) > 0.01);
            bool bodyCheck = (touch.z == 0.0);

            float2 v = e.velocity.xz;
            if (easeOut)
            {
                float2 outDir = -normalize(touch.xy);
                float into = -dot(v, outDir);   // > 0 = toward the wall
                if (into > 0.0)
                    v += outDir * into;
                // falling past a cliff face: clear it faster, the face would cut the body on the way down
                v += outDir * (airborne ? kWallPushOutFalling : kWallPushOut);
            }

            float2 next = from + v * g_Step;
            float2 nx = float2(next.x, from.y);
            float2 nz = float2(from.x, next.y);
            if (StepOk(from, next, radius, bodyCheck, allowDrop, footH))
                e.position.xz = next;
            else if (StepOk(from, nx, radius, bodyCheck, allowDrop, footH))
                e.position.xz = nx;
            else if (StepOk(from, nz, radius, bodyCheck, allowDrop, footH))
                e.position.xz = nz;
        }
        if (enemyExtra[i].kind == SWARM_KIND_GHOST)
        {
            // ghosts hover over everything: snap to the ground below, no falling
            e.position.y = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz);
            e.velocity.y = 0.0;
        }
        else
            Settle(e, radius, allowDrop);

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
    enemies[i].velocity.y = e.velocity.y;   // fall speed (xz is the AI's)
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