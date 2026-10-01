// ============================================================
// SwarmEnemyAICS.hlsl
// One thread per enemy slot. Decide this step's velocity:
//   seek (flow field toward the player, straight line when close)
//   + separation (3x3 spatial hash cells) + avoid (soft push from
//   blocked cells) + hard block (kill the axis that would enter a
//   blocked cell)
//
// Writes velocity ONLY. Position is integrated by SwarmEnemyMoveCS
// in a separate dispatch, so every thread here reads a consistent
// snapshot of positions -- same reason the CPU version copies all
// positions into a vector first.
//
// Logic mirrors ChaseAISystem.cpp. Keep them in sync.
// + player block (solid circle, slide around it)
// Elites (bigger body) stop farther out: SwarmKindScale.
// ============================================================
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<uint> terrain : register(t1);
StructuredBuffer<uint> cellCount : register(t2);   // spatial hash (SwarmEnemyBinCS)
StructuredBuffer<uint> cellItems : register(t3);
StructuredBuffer<float2> flowField : register(t4); // per-cell direction to the player (CPU FlowField)
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5); // kind + fuse (ContactCS lights it)
StructuredBuffer<float> terrainHeight : register(t6);         // cliffs block like walls (SwarmSlopeOk)
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);

// straight-line chase inside this many cells of the player: the flow
// field's per-cell steps would make the ring around the player jitter
static const float kDirectChaseCells = 1.5;

// flow direction at a world xz: bilinear over the 4 nearest cell centres
// so enemies do not snap to a new heading at every cell border.
// (0,0) cells (unreachable / target) pull the blend toward zero, which
// the caller treats as "no path, chase directly"
float2 FlowAt(float2 xz)
{
    float fx = (xz.x - g_GridOrigin.x) / g_CellSize - 0.5;
    float fz = (xz.y - g_GridOrigin.z) / g_CellSize - 0.5;
    int ix = (int) floor(fx), iz = (int) floor(fz);
    float tx = fx - ix, tz = fz - iz;
    int w = (int) g_GridW, d = (int) g_GridD;

    float2 sum = float2(0, 0);
    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        int cx = ix + (k & 1), cz = iz + (k >> 1);
        float wgt = ((k & 1) ? tx : 1.0 - tx) * ((k >> 1) ? tz : 1.0 - tz);
        bool inside = (cx >= 0 && cx < w && cz >= 0 && cz < d);
        sum += inside ? flowField[cz * w + cx] * wgt : float2(0, 0);
    }
    return sum;
}


[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    // ---- hit stun / lit bomber: stand still ----
    // velocity 0 so the exit ramps back up through the lag below.
    // A lit bomber stays put until it blows up, so the player can outrun the blast
    SwarmEnemyExtra extra = enemyExtra[i];
    bool lit = (extra.kind == SWARM_KIND_BOMBER) && (extra.fuse > 0.0);
    if (enemies[i].animIndex == 2u || lit)
    {
        // y is the fall speed (MoveCS owns it): a stunned enemy in mid-air keeps falling
        enemies[i].velocity = float3(0, enemies[i].velocity.y, 0);
        return;
    }

    float3 pos = enemies[i].position;
    float3 oldV = enemies[i].velocity; // own slot: written by this thread last step
    float moveSpeed = enemies[i].moveSpeed;

    // ---- seek: follow the flow field, chase directly when close ----
    // the field routes around boxes and walls (the straight line used
    // to pin enemies against the far side of an obstacle). Near the
    // player, or where the field has no answer, fall back to the line
    float3 moveDir = float3(0, 0, 0);
    if (g_PlayerAlive != 0u)
    {
        float3 toPlayer = g_PlayerPos - pos;
        toPlayer.y = 0.0;
        float lenSq = dot(toPlayer, toPlayer);
        float direct = kDirectChaseCells * g_CellSize;

        float2 flow = FlowAt(pos.xz);
        // near but on another level (player on a plateau above / below):
        // the straight line runs into the cliff, keep following the field to a ramp
        bool sameLevel = abs(g_PlayerPos.y - pos.y) < 1.0;
        bool useFlow = (lenSq > direct * direct || !sameLevel) && (dot(flow, flow) > 0.01);
        if (extra.kind == SWARM_KIND_GHOST)
            useFlow = false; // ghosts fly straight at the player (through walls and over plateaus)
        if (useFlow)
            moveDir = normalize(float3(flow.x, 0.0, flow.y));
        else if (lenSq > 1e-6)
            moveDir = toPlayer * rsqrt(lenSq);
    }

    // ---- separation: live neighbours in the 3x3 cells around us ----
    // was an all-pairs loop over the whole 4096 pool; the spatial hash
    // keeps it at (alive in 9 cells). g_SeparationRadius must stay
    // <= g_CellSize or neighbours two cells away get missed
    float3 sep = float3(0, 0, 0);
    float sepRadSq = g_SeparationRadius * g_SeparationRadius;

    int gx = (int) floor((pos.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((pos.z - g_GridOrigin.z) / g_CellSize);
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int cx = gx + dx, cz = gz + dz;
            if (cx < 0 || cx >= (int) g_GridW || cz < 0 || cz >= (int) g_GridD)
                continue;
            uint cell = (uint) (cz * (int) g_GridW + cx);
            uint n = min(cellCount[cell], SWARM_BUCKET_CAP);
            for (uint k = 0u; k < n; ++k)
            {
                uint j = cellItems[cell * SWARM_BUCKET_CAP + k];
                if (j == i)
                    continue;

                float3 away = pos - enemies[j].position;
                // another level (on a plateau above / below, or falling past): not touching.
                // The crowd at a cliff foot used to hold the ones on the edge above back
                if (abs(away.y) > 1.5)
                    continue;
                away.y = 0.0;
                float dSq = dot(away, away);
                if (dSq > sepRadSq || dSq < 1e-6)
                    continue;

                float dist = sqrt(dSq);
                sep += away / dist * (1.0 - dist / g_SeparationRadius);
            }
        }
    }

    // ---- avoid: soft push away from blocked neighbour cells (not for ghosts) ----
    bool ghost = (extra.kind == SWARM_KIND_GHOST);
    float3 avoid = float3(0, 0, 0);
    float cs = g_CellSize;
    if (!ghost)
    {
        if (!SwarmIsWalkable(terrain, pos + float3(cs, 0, 0)))
            avoid.x -= 1.0;
        if (!SwarmIsWalkable(terrain, pos - float3(cs, 0, 0)))
            avoid.x += 1.0;
        if (!SwarmIsWalkable(terrain, pos + float3(0, 0, cs)))
            avoid.z -= 1.0;
        if (!SwarmIsWalkable(terrain, pos - float3(0, 0, cs)))
            avoid.z += 1.0;
    }

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

    // ---- player is solid ----
    // Same idea as the terrain hard block, but against a circle:
    // drop the velocity component that would carry us inside the
    // contact radius, keep the tangential part so the crowd slides
    // around and rings the player instead of piling onto them.
    // An enemy already overlapping is pushed out gently.
    // Only on the player's level: an enemy on a plateau edge right above the
    // player is not touching it and has to step off and drop (2026-10-01)
    if (g_PlayerAlive != 0u && abs(g_PlayerPos.y - pos.y) < 1.5)
    {
        float3 toP = g_PlayerPos - pos;
        toP.y = 0.0;
        float dist = length(toP);
        float contact = g_PlayerRadius + g_EnemyRadius * SwarmKindScale(extra.kind);

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

    // ---- hard block, applied LAST (after smoothing and the player push) ----
    // the smoothed velocity may still carry an old component into a
    // wall, and the player push-out used to come after this check and shove
    // enemies into trees / walls (2026-09-30). Look-ahead = body radius +
    // velocity * g_LookAhead, so the mesh stops at the wall face instead of
    // sinking half a body into it. A cliff (too steep, see SwarmSlopeOk)
    // counts as a wall: the axis that would climb or drop it is dropped, so
    // the enemy slides along the plateau side and the flow field leads it to
    // a ramp. The diagonal is checked too (both axes free but the corner
    // cell blocked used to let enemies cut through the corner).
    // An enemy already inside a blocked cell skips this: MoveCS walks it out.
    // With the player lower down (SwarmDropAllowed) a cliff going DOWN is no
    // wall: the enemy walks off the edge and falls (2026-10-01)
    if (!ghost && SwarmIsWalkable(terrain, pos))
    {
        float r = SwarmBodyWallRadius(extra.kind);   // same body radius as MoveCS / PushCS
        bool allowDrop = SwarmDropAllowed(terrainHeight, pos.xz);
        float ax = (v.x != 0.0) ? v.x * g_LookAhead + sign(v.x) * r : 0.0;
        float az = (v.z != 0.0) ? v.z * g_LookAhead + sign(v.z) * r : 0.0;

        if (ax != 0.0 && (!SwarmIsWalkable(terrain, pos + float3(ax, 0, 0))
                          || !SwarmStepHeightOk(terrainHeight, pos.xz, pos.xz + float2(ax, 0), allowDrop)))
            v.x = 0.0;
        if (az != 0.0 && (!SwarmIsWalkable(terrain, pos + float3(0, 0, az))
                          || !SwarmStepHeightOk(terrainHeight, pos.xz, pos.xz + float2(0, az), allowDrop)))
            v.z = 0.0;
        if (v.x != 0.0 && v.z != 0.0 && !SwarmIsWalkable(terrain, pos + float3(ax, 0, az)))
        {
            // corner: keep the axis that has more room, drop the other
            if (abs(v.x) > abs(v.z)) v.z = 0.0; else v.x = 0.0;
        }
    }
    v.y = oldV.y;   // the fall speed is MoveCS's (gravity), the AI only steers xz
    enemies[i].velocity = v;
}