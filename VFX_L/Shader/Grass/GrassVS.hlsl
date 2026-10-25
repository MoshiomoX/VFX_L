// ============================================================
// GrassVS.hlsl
// One blade per instance (GrassCullCS's append buffer, drawn with
// DrawInstancedIndirect), 21 vertices from SV_VertexID, no vertex
// buffer (2026-10-23; before: 9 vertices, a quad and a tip triangle).
//
// GrassCullCS animates the blade once and stores its spine as a cubic
// Bezier (root, c1, c2, tip). Rows at t = 0, 1/4, 2/4, 3/4 make three
// quads and a triangle closes the tip. The width narrows toward the tip
// and the width axis turns with the blade's twist, so a blade curled by
// a twister shows a twisted ribbon.
// ============================================================
#include "GrassCommon.hlsli"

StructuredBuffer<GrassBlade> g_Blades : register(t0);
Texture2D<float4> g_Ground : register(t1);
SamplerState s_Linear : register(s0);

struct GrassOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : TEXCOORD2;  // ground color under the blade * its shade
    float t : TEXCOORD3;       // 0 root .. 1 tip
    float glow : TEXCOORD4;    // ember glow 0..1 (fire)
    float sheen : TEXCOORD5;   // 0..1 the lighter underside shows
    float dry : TEXCOORD6;     // 0..1 a dry, yellow blade
};

GrassOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    GrassBlade b = g_Blades[iid];

    // which point of the blade: 3 quads (6 vertices each) between rows 0..3, then the tip triangle.
    // across: -1 / +1 = the two edges, 0 = the tip
    float t;
    float across;
    if (vid < 18u)
    {
        uint q = vid / 6u;
        uint c = vid % 6u;
        uint row = q + ((c == 2u || c == 3u || c == 5u) ? 1u : 0u);
        t = (float) row * 0.25;
        across = (c == 1u || c == 4u || c == 5u) ? 1.0 : -1.0;
    }
    else
    {
        uint c = vid - 18u;
        t = (c == 2u) ? 1.0 : 0.75;
        across = (c == 0u) ? -1.0 : ((c == 1u) ? 1.0 : 0.0);
    }

    float u = 1.0 - t;
    float3 spine = b.c1 * (3.0 * u * u * t) + b.c2 * (3.0 * u * t * t) + b.tip * (t * t * t);
    // width axis: the root axis turned around the vertical by the twist so far
    float a = b.twist * t;
    float2 w2 = b.side * cos(a) + float2(-b.side.y, b.side.x) * sin(a);
    float halfW = b.width * 0.5 * (1.0 - 0.8 * pow(t, 1.5));
    float3 p = b.pos + spine + float3(w2.x, 0.0, w2.y) * (across * halfW);

    GrassOut o;
    o.world = p;
    o.pos = mul(float4(p, 1.0), g_ViewProj);
    // mostly up (the field shades like the ground under it), leaning a little with the blade's face
    float2 face = float2(-w2.y, w2.x);
    o.normal = normalize(float3(0.0, 1.0, 0.0) + float3(face.x, 0.0, face.y) * 0.25);
    // charred: the ground colour darkens toward soot (keeps its hue)
    float2 uv = (b.pos.xz - g_MapOrigin) / g_MapSize;
    o.color = g_Ground.SampleLevel(s_Linear, uv, 0).rgb * b.shade * lerp(1.0, 0.06, pow(b.burn, 0.6));
    o.t = t;
    o.glow = b.glow;
    o.sheen = b.sheen;
    o.dry = b.dry;
    return o;
}
