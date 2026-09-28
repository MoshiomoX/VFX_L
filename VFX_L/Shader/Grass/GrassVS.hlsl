// ============================================================
// GrassVS.hlsl
// One blade per instance (GrassCullCS's append buffer, drawn with
// DrawInstancedIndirect), 9 vertices from SV_VertexID, no vertex
// buffer: a root quad narrowing to 0.6 at half height + a tip triangle.
//
// The tip is pushed sideways by the blade's own curl, the wind (waves
// travelling along g_WindDir + a small flutter) and the trample map.
// The blade keeps its length: the more it bends, the lower the tip.
// The sideways offset grows with t^2 so the blade curves.
// ============================================================
#include "GrassCommon.hlsli"

StructuredBuffer<GrassBlade> g_Blades : register(t0);
Texture2D<float2> g_Trample : register(t1);
Texture2D<float4> g_Ground : register(t2);
SamplerState s_Linear : register(s0);

struct GrassOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : TEXCOORD2;  // ground color under the blade * its shade
    float t : TEXCOORD3;       // 0 root .. 1 tip
};

// x = across the blade (-1..1, times half the width), y = t
static const float2 kShape[9] =
{
    float2(-1.0, 0.0), float2(1.0, 0.0), float2(-0.6, 0.5),
    float2(-0.6, 0.5), float2(1.0, 0.0), float2(0.6, 0.5),
    float2(-0.6, 0.5), float2(0.6, 0.5), float2(0.0, 1.0)
};

GrassOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    GrassBlade b = g_Blades[iid];
    float2 s = kShape[vid];
    float t = s.y;

    float2 side = float2(cos(b.yaw), sin(b.yaw));
    float2 facing = float2(-side.y, side.x);
    float2 uv = (b.pos.xz - g_MapOrigin) / g_MapSize;

    // wind: mostly calm with gusts rolling across the field
    float phase = dot(b.pos.xz, g_WindDir) * g_WindScale * 6.2831853 - g_Time * g_WindSpeed;
    float gust = sin(phase) * 0.5 + 0.5;
    gust *= gust;
    float flutter = sin(g_Time * 3.1 + b.yaw * 5.0) * 0.15;
    float2 trample = g_Trample.SampleLevel(s_Linear, uv, 0);

    float2 bend = facing * b.curl
        + g_WindDir * (g_WindStrength * (0.25 + gust + flutter))
        + trample * g_TrampleBend;
    bend *= b.height;
    float bl = length(bend);
    float maxBend = 0.92 * b.height;
    if (bl > maxBend)
    {
        bend *= maxBend / bl;
        bl = maxBend;
    }
    float tipY = sqrt(max(b.height * b.height - bl * bl, 0.0));

    float3 p = b.pos
        + float3(side.x, 0.0, side.y) * (s.x * b.width * 0.5)
        + float3(bend.x, 0.0, bend.y) * (t * t)
        + float3(0.0, tipY * t, 0.0);

    GrassOut o;
    o.world = p;
    o.pos = mul(float4(p, 1.0), g_ViewProj);
    // mostly up (the field shades like the ground under it), leaning a little with the blade
    o.normal = normalize(float3(0.0, 1.0, 0.0) + float3(facing.x, 0.0, facing.y) * 0.25);
    o.color = g_Ground.SampleLevel(s_Linear, uv, 0).rgb * b.shade;
    o.t = t;
    return o;
}
