// ============================================================
// SwarmEnemyAICS.hlsl
// One thread per enemy slot. Decide this step's velocity:
//   seek (toward player) + separation (all-to-all against other
//   alive enemies) + avoid (soft push from blocked cells)
//   + hard block (kill the axis that would enter a blocked cell)
//
// Writes velocity ONLY. Position is integrated by SwarmEnemyMoveCS
// in a separate dispatch, so every thread here reads a consistent
// snapshot of positions -- same reason the CPU version copies all
// positions into a vector first.
//
// Logic mirrors ChaseAISystem.cpp. Keep them in sync.
// + player block (solid circle, slide around it)
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<uint> terrain : register(t1);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);


[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    float3 pos = enemies[i].position;
    float3 oldV = enemies[i].velocity; // own slot: written by this thread last step
    float moveSpeed = enemies[i].moveSpeed;

    // ---- seek ----
    float3 moveDir = float3(0, 0, 0);
    if (g_PlayerAlive != 0u)
    {
        moveDir = g_PlayerPos - pos;
        moveDir.y = 0.0;
        float lenSq = dot(moveDir, moveDir);
        if (lenSq > 1e-6)
            moveDir *= rsqrt(lenSq);
    }

    // ---- separation: every alive neighbour inside the radius ----
    // no early-out: the ALU is cheap and dropping the branch keeps
    // the warp converged. dead slots are skipped by state
    float3 sep = float3(0, 0, 0);
    float sepRadSq = g_SeparationRadius * g_SeparationRadius;

    for (uint j = 0; j < g_MaxEnemies; ++j)
    {
        if (j == i)
            continue;
        if (enemyStates[j] == SWARM_DEAD)
            continue;

        float3 away = pos - enemies[j].position;
        away.y = 0.0;
        float dSq = dot(away, away);
        if (dSq > sepRadSq || dSq < 1e-6)
            continue;

        float dist = sqrt(dSq);
        sep += away / dist * (1.0 - dist / g_SeparationRadius);
    }

    // ---- avoid: soft push away from blocked neighbour cells ----
    float3 avoid = float3(0, 0, 0);
    float cs = g_CellSize;
    if (!SwarmIsWalkable(terrain, pos + float3(cs, 0, 0)))
        avoid.x -= 1.0;
    if (!SwarmIsWalkable(terrain, pos - float3(cs, 0, 0)))
        avoid.x += 1.0;
    if (!SwarmIsWalkable(terrain, pos + float3(0, 0, cs)))
        avoid.z -= 1.0;
    if (!SwarmIsWalkable(terrain, pos - float3(0, 0, cs)))
        avoid.z += 1.0;

    // ---- compose the target velocity ----
    float3 target = moveDir * moveSpeed
                  + sep * g_SeparationPower
                  + avoid * g_AvoidPower;
    target.y = 0.0;

    // ---- speed cap: separation may stack up far beyond moveSpeed ----
    float maxSpeed = moveSpeed * g_MaxSpeedMul;
    float tLenSq = dot(target, target);
    if (tLenSq > maxSpeed * maxSpeed)
        target *= maxSpeed * rsqrt(tLenSq);

    // ---- inertia: exponential approach to the target ----
    // frame-rate independent because g_Step is the fixed step
    float k = 1.0 - exp(-g_VelocityLag * g_Step);
    float3 v = lerp(oldV, target, k);

    // ---- hard block, applied AFTER smoothing ----
    // the smoothed velocity may still carry an old component into a
    // wall, so the check has to see the value that will be integrated
    if (SwarmIsWalkable(terrain, pos))
    {
        float ax = v.x * g_LookAhead;
        float az = v.z * g_LookAhead;

        if (ax != 0.0 && !SwarmIsWalkable(terrain, pos + float3(ax, 0, 0)))
            v.x = 0.0;
        if (az != 0.0 && !SwarmIsWalkable(terrain, pos + float3(0, 0, az)))
            v.z = 0.0;
    }
      // ---- player is solid ----
    // Same idea as the terrain hard block, but against a circle:
    // drop the velocity component that would carry us inside the
    // contact radius, keep the tangential part so the crowd slides
    // around and rings the player instead of piling onto them.
    // An enemy already overlapping is pushed out gently.
    if (g_PlayerAlive != 0u)
    {
        float3 toP = g_PlayerPos - pos;
        toP.y = 0.0;
        float dist = length(toP);
        float contact = g_PlayerRadius + g_EnemyRadius;

        float3 n;
        if (dist > 1e-4)
            n = toP / dist;
        else
        {
            // dead centre: pick a direction from the slot index so
            // stacked enemies scatter instead of all pushing the same way
            float a = (float) i * 2.399;
            n = float3(cos(a), 0.0, sin(a));
            dist = 0.0;
        }

        float along = dot(v, n); // > 0 = moving toward the player
        float nextDist = dist - along * g_Step;

        if (along > 0.0 && nextDist < contact)
            v -= n * along;

        if (dist < contact)
            v -= n * (contact - dist) * g_PlayerPushOut;
    }
    v.y = 0.0;
    enemies[i].velocity = v;
}