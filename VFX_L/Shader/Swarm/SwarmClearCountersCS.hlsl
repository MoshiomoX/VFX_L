// ============================================================
// SwarmClearCountersCS.hlsl
// Zero the per-step counters before each sub-step.
//
// Alive counts are recomputed from scratch every step, so they are
// cleared here.
//
// Kill count and player damage ACCUMULATE forever. The CPU stores
// the last value it saw and takes the difference. This is deliberate:
// readback uses DO_NOT_WAIT and can fail on any given frame, and a
// per-frame reset would silently lose those events. A uint would
// need 4 billion kills to wrap.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

RWByteAddressBuffer counters : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    counters.Store(SWARM_CNT_ALIVE_ENEMIES, 0u);
    counters.Store(SWARM_CNT_ALIVE_PROJ, 0u);
    counters.Store(SWARM_CNT_NEAREST_KEY, SWARM_NO_TARGET_KEY);
   // counters.Store(SWARM_CNT_KILLS, 12345u); // TEMP: readback probe
    // do NOT touch SWARM_CNT_KILLS / SWARM_CNT_PLAYER_DAMAGE
}