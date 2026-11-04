// ============================================================
// WaterVS.hlsl
// 池の水面（World/WaterSurface）。頂点バッファなし：メッシュは
// float4（ワールド位置、岸までの距離）の構造化バッファで、
// 三角形 1 つに 3 個、SV_VertexID で読む。
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
