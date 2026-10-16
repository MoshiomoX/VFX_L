// ============================================================
// SwarmProjTrail.hlsli
// Shared by SwarmProjTrailCS / SwarmProjTrailVS: the extra bits of the
// per-projectile trail state on top of ParticleTrail.hlsli's
// head / count packing (2026-10-15).
// ============================================================
#ifndef SWARM_PROJ_TRAIL_HLSLI
#define SWARM_PROJ_TRAIL_HLSLI

// bit 16: the projectile died; the ribbon is draining into the impact point
#define PROJ_TRAIL_DRAINING (1u << 16)
// bits 17-24: samples pushed since the death (TRAIL_POINTS = fully drained)
#define PROJ_TRAIL_DRAIN_SHIFT 17u

#endif
