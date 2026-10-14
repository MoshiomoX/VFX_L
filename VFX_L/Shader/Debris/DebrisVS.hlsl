// ============================================================
// DebrisVS.hlsl
// One piece of ground debris per instance (DrawInstanced(6, side^2),
// no vertex buffer): a small card (two triangles) turned by the
// piece's euler rotation, flat on the ground at rest and tumbling
// in the air. Hidden pieces and pieces at the window's edge shrink to
// nothing (the edge is where a row re-seats when the camera moves).
// The PS cuts the leaf / blade shape out of the card.
// ============================================================
#include "DebrisCommon.hlsli"

StructuredBuffer<Debris> g_Debris : register(t0);

struct DebrisOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;      // -1..1 across the card (x along, y across)
    nointerpolation uint seed : TEXCOORD3;
};

static const float2 kQuad[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

DebrisOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    Debris d = g_Debris[iid];
    DebrisOut o;
    o.seed = d.seed;
    o.uv = kQuad[vid];

    float2 size = DebrisSize(d.seed);
    // fade at the window edge (Chebyshev distance: the window is a square)
    float2 dc = abs(d.pos.xz - g_CamPos.xz);
    float edge = saturate((g_Window - max(dc.x, dc.y)) / max(g_FadeWidth, 0.01));
    float scale = edge;
    if ((d.seed & DEBRIS_HIDDEN) != 0u)
        scale = 0.0;

    float3x3 R = DebrisRotation(d.rot);
    float3 local = float3(o.uv.x * size.x * 0.5, 0.0, o.uv.y * size.y * 0.5) * scale;
    float3 world = d.pos + mul(R, local);
    float3 n = mul(R, float3(0, 1, 0));
    // two-sided: face the camera
    if (dot(n, g_CamPos - world) < 0.0)
        n = -n;

    o.world = world;
    o.normal = n;
    o.pos = mul(float4(world, 1.0), g_ViewProj);
    return o;
}
