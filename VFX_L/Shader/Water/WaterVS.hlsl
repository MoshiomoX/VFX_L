// ============================================================
// WaterVS.hlsl
// The pond water surface (World/WaterSurface). No vertex buffer: the
// mesh is a structured buffer of float4 (world position, distance to
// the shore), three per triangle, read by SV_VertexID.
// ============================================================
#include "WaterCommon.hlsli"

StructuredBuffer<float4> g_Verts : register(t0);

WaterOut main(uint vid : SV_VertexID)
{
    float4 v = g_Verts[vid];
    WaterOut o;
    o.world = v.xyz;
    o.shore = v.w;
    o.pos = mul(float4(v.xyz, 1.0), g_ViewProj);
    return o;
}
