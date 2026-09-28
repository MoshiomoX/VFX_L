// ============================================================
// SwarmOrbEmitCS.hlsl
// Trail particles for exp orbs being pulled toward the player.
// Same emit body as SwarmEmitCS; the sources are the live orbs whose
// pull speed is over g_OrbTrailMinSpeed, all using one recipe
// (VFXId::ExpOrbTrail, Assets/Data/VFXData/ExpOrbTrail.json).
// Idle orbs emit nothing, so a field full of orbs costs no particles.
// ============================================================
#define SWARM_EMIT_ORBS
#include "SwarmEmitCS.hlsl"
