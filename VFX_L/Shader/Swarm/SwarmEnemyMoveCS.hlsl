// ============================================================
// SwarmEnemyMoveCS.hlsl
// One fixed step of enemy integration. Runs after SwarmEnemyAICS
// so every velocity is final before any position moves.
//
// No gravity, no falling: the ground is flat, so y is pinned to
// g_GroundY. yaw is stored in radians (the VS does sin/cos on it
// directly; the CPU version stored degrees for TransformComponent).
//
// Also counts alive enemies for the readback.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
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

    // ---- integrate ----
    // no terrain check here: the AI pass already refused to move
    // into a blocked cell (hard block). re-checking would only
    // trap an enemy that is already inside a wall
    e.position.x += e.velocity.x * g_Step;
    e.position.z += e.velocity.z * g_Step;
    e.position.y = g_GroundY;

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

    enemies[i].position = e.position;
    enemies[i].yaw = e.yaw;
    enemies[i].animTime = e.animTime;

    uint prev;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_ENEMIES, 1u, prev);
     // ---- aim: compete for "closest to the player" ----
    // XZ distance only, same as everything else in this pipeline
    float3 dp = e.position - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);
    uint key = (asuint(dist) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK);
    counters.InterlockedMin(SWARM_CNT_NEAREST_KEY, key, prev);
}