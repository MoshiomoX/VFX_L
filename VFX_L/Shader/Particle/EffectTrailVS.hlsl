// ============================================================
// EffectTrailVS.hlsl
// Builds a camera-facing ribbon per effect trail from its ring.
// No vertex buffer: one instance = one trail, SV_VertexID walks a
// triangle strip of EFFECT_TRAIL_STRIP_PAIRS pairs (see
// Common/EffectTrail.hlsli for the pair layout).
//
// Pixel side is ParticleTrailPS (same TrailDrawCB / uv / softEdge).
// One draw per style (the texture and blend differ), so instances of
// another style are culled here.
// ============================================================
#include "Common/ParticleTrail.hlsli"
#include "Common/EffectTrail.hlsli"

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    float _pad0;
};

cbuffer TrailDrawCB : register(b1)
{
    uint g_StyleSlot; // style index + 1
    uint g_Premultiply;
    float g_Time; // same clock as EffectTrailCB.g_Now
    float _padT;
};

StructuredBuffer<uint> trailAlive : register(t0);
StructuredBuffer<EffectTrailAnchor> anchors : register(t1);
StructuredBuffer<EffectTrailState> states : register(t2);
StructuredBuffer<EffectTrailPoint> points : register(t3);
StructuredBuffer<TrailStyle> trailStyles : register(t4);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation float softEdge : TEXCOORD1;
};

// strip pair k -> position / path length.
// Anything past validCount is the tail (callers clamp k to validCount + 1)
void StripPoint(EffectTrailState st, uint base, uint k,
                out float3 pos, out float dist)
{
    pos = st.tailPos;
    dist = st.tailDist;

    if (k == 0u)
    {
        pos = st.headPos;
        dist = st.headDist;
    }
    else if (k <= st.validCount)
    {
        EffectTrailPoint p = points[base + EffectTrailSlot(st.ringHead, k - 1u)];
        pos = p.position;
        dist = p.distance;
    }
}

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;
    o.position = float4(0, 0, -1, 1); // culled unless we get to the end

    uint i = trailAlive[instanceID];
    EffectTrailAnchor a = anchors[i];
    if (a.styleSlot != g_StyleSlot)
        return o;

    EffectTrailState st = states[i];
    TrailStyle s = trailStyles[a.styleSlot - 1u];
    uint base = i * EFFECT_TRAIL_POINTS;

    uint last = st.validCount + 1u; // the tail pair
    uint k = min(vertexID / 2u, last);
    float sideSign = ((vertexID & 1u) == 0u) ? 1.0 : -1.0;

    float3 pos, posPrev, posNext;
    float dist, dDummy;
    StripPoint(st, base, k, pos, dist);
    StripPoint(st, base, (k > 0u) ? k - 1u : 0u, posPrev, dDummy);
    StripPoint(st, base, min(k + 1u, last), posNext, dDummy);

    // direction of travel (tail -> head). Right after a commit the head
    // sits on the newest point, so look one point further
    float3 tangent = posPrev - posNext;
    if (dot(tangent, tangent) < 1e-10)
    {
        float3 posNext2;
        StripPoint(st, base, min(k + 2u, last), posNext2, dDummy);
        tangent = pos - posNext2;
    }

    float3 toCam = g_CameraPosition - pos;
    float3 side = cross(tangent, toCam);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-12) ? side * rsqrt(sideLenSq) : float3(0, 0, 0);

    // 0 = head, 1 = tail, by position along the ribbon (like Unity's
    // TrailRenderer). Not by age: a trail younger than lifetime would
    // end in an opaque, full-width stub at its start point. A trail that
    // stops just gets shorter; the head stays at colorHead until it is gone
    float span = st.headDist - st.tailDist;
    float t = (span > 1e-5) ? saturate((st.headDist - dist) / span) : 0.0;

    float width = lerp(s.widthHead, s.widthTail, t);
    float3 worldPos = pos + side * (sideSign * width * 0.5);

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    float4 c = lerp(s.colorHead, s.colorTail, t);
    c.rgb *= s.intensity;
    o.color = c;

    float u = ((s.flags & TRAIL_FLAG_UV_TILE) != 0u) ? dist * s.uvRepeat : t * s.uvRepeat;
    o.uv = float2(u + g_Time * s.uvScroll, (sideSign > 0.0) ? 0.0 : 1.0);
    o.softEdge = s.softEdge;
    return o;
}
