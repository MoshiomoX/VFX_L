// ============================================================
// SwarmAimResolveCS.hlsl
// Step 5 of DispatchStep: after the hits and the area damage (4 / 4c),
// before the contact (6). The key was written by MoveCS (step 2) for an
// enemy that was alive then; if HitCS / AreaDamageCS killed it since, the
// CPU fires one shot at a corpse, which is harmless. Not checking state
// here avoids reporting "no target" every time the closest enemy dies.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
RWByteAddressBuffer counters : register(u0);
// the plain closest enemy (2026-10-27, MoveCS u4): +0 key, +4 pos, +16 dist. The key is reset here for the next step
RWByteAddressBuffer closeTarget : register(u1);

[numthreads(1, 1, 1)]
void main()
{
    // the plain closest (no boss / elite priority)
    {
        uint ckey = closeTarget.Load(0);
        if (ckey == SWARM_NO_TARGET_KEY)
            closeTarget.Store(16, asuint(1e30));
        else
        {
            SwarmEnemy c = enemies[ckey & SWARM_SLOT_MASK];
            float3 cp = c.position - g_PlayerPos;
            closeTarget.Store3(4, asuint(c.position));
            closeTarget.Store(16, asuint(sqrt(cp.x * cp.x + cp.z * cp.z)));
        }
        closeTarget.Store(0, SWARM_NO_TARGET_KEY);
    }

    uint key = counters.Load(SWARM_CNT_NEAREST_KEY);
    uint slot = key & SWARM_SLOT_MASK;

    if (key == SWARM_NO_TARGET_KEY )
    {
        counters.Store(SWARM_CNT_NEAREST_DIST, asuint(1e30));
        return;
    }

    SwarmEnemy e = enemies[slot];
    float3 dp = e.position - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);

    counters.Store3(SWARM_CNT_NEAREST_POS, asuint(e.position));
    counters.Store3(SWARM_CNT_NEAREST_VEL, asuint(e.velocity));
    counters.Store(SWARM_CNT_NEAREST_DIST, asuint(dist));
}
