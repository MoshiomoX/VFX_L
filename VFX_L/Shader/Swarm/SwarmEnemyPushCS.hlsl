// ============================================================
// SwarmEnemyPushCS.hlsl
// Overlap resolve, runs right after SwarmEnemyMoveCS.
//
// Separation in the AI is only a velocity term: once the front row
// stops at the player, the rows behind still push in and the crowd
// stacks into one pile. This pass is a position constraint: every
// pair closer than 2 * g_EnemyRadius is pushed apart, each side by
// half the penetration (Jacobi, one iteration per fixed step -- the
// steps are short and the crowd settles over a few of them).
//
// Neighbours come from the spatial hash built at the start of the
// step, so a cell may be one step stale; the positions themselves
// are read live from the enemies buffer (another thread may already
// have moved its owner this pass -- fine for a soft solver).
//
// A push that would end in a blocked cell is dropped: the walls are
// hard, the crowd is not.
// ============================================================
#define SWARM_BOMBER_CB_REG b3   // kind body scale (the wall gap of elites / boss)
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<uint> terrain : register(t1);
StructuredBuffer<uint> cellCount : register(t2);
StructuredBuffer<uint> cellItems : register(t3);
StructuredBuffer<float> terrainHeight : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5); // kind: ghosts ignore walls and cliffs
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);

// how much of the penetration to close per step (1 = all of it, jittery)
static const float kPushRelax = 0.6;
// cap so a deep pile does not teleport; in units of enemy radius
static const float kPushMaxRadii = 0.75;
// rest distance in units of the collision diameter. > 1 so the meshes
// (skull ~0.9m wide vs 0.8m collision diameter) do not visibly intersect
static const float kPushDistMul = 1.15;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    float3 pos = enemies[i].position;
    float minDist = 2.0 * g_EnemyRadius * kPushDistMul;
    float minDistSq = minDist * minDist;

    float3 push = float3(0, 0, 0);

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
                if (enemyStates[j] == SWARM_DEAD)
                    continue;

                float3 away = pos - enemies[j].position;
                away.y = 0.0;
                float dSq = dot(away, away);
                if (dSq >= minDistSq)
                    continue;

                float dist;
                if (dSq > 1e-6)
                    dist = sqrt(dSq);
                else
                {
                    // exactly stacked: split by slot index so they part
                    float a = (float) (i * 7u + j * 3u) * 0.618;
                    away = float3(cos(a), 0.0, sin(a));
                    dist = 1e-3;
                }
                push += away / dist * ((minDist - dist) * 0.5);
            }
        }
    }

    float pLenSq = dot(push, push);
    if (pLenSq < 1e-8)
        return;

    push *= kPushRelax;
    float maxPush = g_EnemyRadius * kPushMaxRadii;
    if (pLenSq * kPushRelax * kPushRelax > maxPush * maxPush)
        push *= maxPush * rsqrt(dot(push, push));

    float3 np = pos + push;
    uint kind = enemyExtra[i].kind;
    bool ghost = (kind == SWARM_KIND_GHOST);   // ghosts ignore walls and cliffs
    if (!ghost)
    {
        // the body circle must not be pushed into a wall / cliff face either
        // (2026-10-01): drop the part of the push that goes into it, and the
        // whole push if the body would still touch more than it does now
        float radius = SwarmBodyWallRadius(kind);
        float3 tn = SwarmBodyContact(terrain, terrainHeight, np.xz, radius);
        if (tn.z > 0.0)
        {
            float tl = length(tn.xy);
            if (tl > 1e-3)
            {
                float2 n = tn.xy / tl;
                float along = dot(push.xz, n);
                if (along > 0.0)
                    push.xz -= n * along;
                np = pos + push;
            }
            if (SwarmBodyContact(terrain, terrainHeight, np.xz, radius).z
                > SwarmBodyContact(terrain, terrainHeight, pos.xz, radius).z)
                return;
        }
        if (!SwarmIsWalkable(terrain, np) || !SwarmSlopeOk(terrainHeight, pos.xz, np.xz))
            return;   // would end up in a wall or over a cliff edge: stay put this step
    }

    np.y = g_GroundY + SwarmTerrainHeight(terrainHeight, np.xz);
    enemies[i].position = np;
}
