// ============================================================
// SwarmProjTrailVS.hlsl
// Camera-facing ribbon per GPU projectile, built straight from the
// slot's history ring (SwarmProjTrailCS). Same geometry as
// ParticleTrailVS: one instance = one projectile slot, SV_VertexID
// walks a triangle strip of (TRAIL_POINTS + 1) pairs.
//
//   pair 0    : live head = the projectile's current position
//   pair 1..N : ring samples, newest first
//
// One draw per style (texture / blend differ); other styles and dead
// slots collapse to a clipped point. Pairs with ParticleTrailPS.
// ============================================================
#include "../Particle/Common/ParticleTrail.hlsli"
#include "../Common/SwarmProjTrail.hlsli"

// SwarmProjectile (SwarmCommon.hlsli, 48 bytes). Declared here so the
// frame cbuffer of SwarmCommon does not take b0
struct ProjLite
{
    float3 position;
    float damage;
    float3 velocity;
    float lifetime;
    float radius;
    uint vfxType;
    uint motion;
    float pathT;
};

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    float _pad0;
};

cbuffer TrailDrawCB : register(b1)
{
    uint g_StyleSlot; // style index + 1 (recipeTrail encoding)
    uint g_Premultiply;
    float g_Time;
    float _padT;
};

StructuredBuffer<ProjLite> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
StructuredBuffer<uint> recipeTrail : register(t2);
StructuredBuffer<TrailStyle> trailStyles : register(t3);
StructuredBuffer<float3> trailPoints : register(t4);
StructuredBuffer<uint> trailState : register(t5);
StructuredBuffer<float> trailAge : register(t6);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation float softEdge : TEXCOORD1;
};

// position of strip point k and its normalized age t (0 head .. 1 tail)
void TrailPointAt(float3 headPos, uint slot, uint head, uint count,
                  float f, float interval, float lifetime, uint k,
                  out float3 pos, out float t)
{
    pos = headPos;
    t = 0.0;

    if (k > 0u)
    {
        uint j = min(k - 1u, count - 1u); // 0 = newest sample
        uint base = slot * TRAIL_POINTS;
        uint ring = (head + TRAIL_POINTS - j) % TRAIL_POINTS;
        pos = trailPoints[base + ring];
        t = saturate(((float) j + f) * interval / lifetime);

        // ring full: slide the oldest sample toward its neighbour so the
        // tail shrinks smoothly instead of jumping one sample at a time
        if (count == TRAIL_POINTS && j == count - 1u)
        {
            uint ringNext = (head + TRAIL_POINTS - (j - 1u)) % TRAIL_POINTS;
            pos = lerp(pos, trailPoints[base + ringNext], f);
            t = 1.0;
        }
    }
}

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;
    o.position = float4(0, 0, -1, 1); // culled unless we get to the end

    uint slot = instanceID;
    uint state = trailState[slot];
    bool draining = (state & PROJ_TRAIL_DRAINING) != 0u;
    // a dead slot still draws while its ribbon drains into the impact point
    if (projStates[slot] == 0u && !draining) // SWARM_DEAD
        return o;
    ProjLite p = projectiles[slot];
    if (recipeTrail[p.vfxType] != g_StyleSlot)
        return o;

    uint head = TrailHead(state);
    uint count = TrailCount(state);
    if (count == 0u)
        return o;
    // draining: thin out as it sinks in (1 -> 0.4 over the drain)
    float drainWidth = draining ? lerp(1.0, 0.4, (float) (state >> PROJ_TRAIL_DRAIN_SHIFT) / (float) TRAIL_POINTS) : 1.0;

    TrailStyle s = trailStyles[g_StyleSlot - 1u];
    float lifetime = max(s.lifetime, 0.01);
    float interval = TrailInterval(s.lifetime);
    float f = frac(trailAge[slot] / interval); // time since the newest sample, in intervals

    uint k = vertexID / 2u;
    float sideSign = ((vertexID & 1u) == 0u) ? 1.0 : -1.0;

    float3 pos, posPrev, posNext;
    float t, tDummy;
    TrailPointAt(p.position, slot, head, count, f, interval, lifetime, k, pos, t);
    TrailPointAt(p.position, slot, head, count, f, interval, lifetime, (k > 0u) ? k - 1u : 0u, posPrev, tDummy);
    TrailPointAt(p.position, slot, head, count, f, interval, lifetime, min(k + 1u, TRAIL_POINTS), posNext, tDummy);

    // direction of travel (tail -> head). Right after a commit the
    // neighbours coincide at the head, so fall back to the velocity
    float3 tangent = posPrev - posNext;
    if (dot(tangent, tangent) < 1e-10)
        tangent = p.velocity;

    float3 toCam = g_CameraPosition - pos;
    float3 side = cross(tangent, toCam);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-12) ? side * rsqrt(sideLenSq) : float3(0, 0, 0);

    float width = lerp(s.widthHead, s.widthTail, t) * drainWidth;
    float3 worldPos = pos + side * (sideSign * width * 0.5);

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    float4 c = lerp(s.colorHead, s.colorTail, t);
    c.rgb *= s.intensity;
    o.color = c;

    o.uv = float2(t * s.uvRepeat + g_Time * s.uvScroll, (sideSign > 0.0) ? 0.0 : 1.0);
    o.softEdge = s.softEdge;
    return o;
}
