// ============================================================
// SwarmOrbMoveCS.hlsl
// One thread per orb slot. Pull toward the player, pick up, count.
//
// Attract state lives in SwarmOrb._pad as the current pull speed:
//   0     -> idle, waiting for the player to come within attract radius
//   > 0   -> attracted. once attracted it never lets go, even if the
//            player leaves the radius (same rule as the CPU version,
//            avoids flip-flopping at the boundary)
//
// Pickup adds amount (fixed point x100) to SWARM_CNT_EXP, which
// accumulates forever; the CPU takes the delta.
//
// Only writer of orb position / velocity / _pad. Runs after HitCS so
// orbs dropped this step are already visible.
//
// velocity is for the looks only (SwarmOrbVS leans / stretches the
// gem along it, SwarmOrbEmitCS sweeps its trail): the horizontal pull
// of this step, 0 while idle.
//
// Height: g_OrbY above the terrain (plateaus, ramps), eased so an orb
// pulled across a plateau edge glides up instead of popping.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<float> terrainHeight : register(t0);

RWStructuredBuffer<SwarmOrb> orbs : register(u0);
RWBuffer<uint> orbStates : register(u1);
RWByteAddressBuffer counters : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxOrbs)
        return;
    if (orbStates[i] == SWARM_DEAD)
        return;

    SwarmOrb o = orbs[i];

    // XZ only, same as the rest of the pipeline
    float dx = g_PlayerPos.x - o.position.x;
    float dz = g_PlayerPos.z - o.position.z;
    float distSq = dx * dx + dz * dz;

    float speed = o._pad;
    bool attracted = speed > 0.0;
    float3 vel = float3(0, 0, 0);

    if (g_PlayerAlive != 0u)
    {
        if (!attracted && distSq <= g_OrbAttractRadius * g_OrbAttractRadius)
        {
            attracted = true;
            speed = g_OrbAccel * g_Step; // kick so speed > 0 next step
        }

        if (attracted)
        {
            // ---- pickup ----
            if (distSq <= g_OrbPickupRadius * g_OrbPickupRadius)
            {
                uint prev;
                counters.InterlockedAdd(SWARM_CNT_EXP,
                                        SwarmHpToFixed(o.amount), prev);
                orbStates[i] = SWARM_DEAD;
                return;
            }

            // ---- pull: faster the closer it gets ----
            speed = min(speed + g_OrbAccel * g_Step, g_OrbMaxSpeed);

            float invLen = rsqrt(max(distSq, 1e-8));
            vel = float3(dx * invLen * speed, 0.0, dz * invLen * speed);
            o.position.x += vel.x * g_Step;
            o.position.z += vel.z * g_Step;
        }
    }

    float targetY = SwarmTerrainHeight(terrainHeight, o.position.xz) + g_OrbY;
    o.position.y = lerp(o.position.y, targetY, 1.0 - exp(-12.0 * g_Step));
    o._pad = speed;

    orbs[i].position = o.position;
    orbs[i].velocity = vel;
    orbs[i]._pad = speed;

    uint prev2;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_ORBS, 1u, prev2);
}