// ============================================================
// SwarmAreaEmitCS.hlsl
// Particles for areas that were born on the GPU (projectile hit):
// only the GPU knows where they are, so the CPU cannot play a VFX
// for them. Same emit body as SwarmEmitCS, the source is the live
// areas. Areas spawned by the CPU carry vfxType 0 and emit nothing
// here -- the CPU plays their effect (mesh + particles).
//
// An explosion is simply a short-lived area with a high emit rate.
// ============================================================
#define SWARM_EMIT_AREAS
#include "SwarmEmitCS.hlsl"
