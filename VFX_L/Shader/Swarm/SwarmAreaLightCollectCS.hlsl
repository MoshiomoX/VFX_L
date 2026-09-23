// ============================================================
// SwarmAreaLightCollectCS.hlsl
// Point lights for areas born on the GPU (projectile hit / lifetime
// end). Same body as SwarmLightCollectCS, the source is the live
// areas. CPU-cast areas carry vfxType 0 and add nothing here -- the
// CPU VFX entry pushes their light itself.
// ============================================================
#define SWARM_LIGHT_AREAS
#include "SwarmLightCollectCS.hlsl"
