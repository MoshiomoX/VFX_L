// ============================================
// EdgeFilterCS.hlsl
// Build the list of source vertices that sit on the dissolve edge.
// One thread per vertex. Same noise lookup as VFXMeshPS:
//   n = noise(uv * tiling + scroll).r
// PS keeps  n >= threshold  and paints  n < threshold + edge  as the
// burning rim, so that band is what we collect here.
// Output is an AppendStructuredBuffer of vertex indices; the caller
// copies its count to a small buffer that EmitCS reads (no readback).
// ============================================
#include "Common/ParticleCommon.hlsli"

// b0 / b1 belong to ParticleCommon (unused here)
cbuffer EdgeFilterCB : register(b2)
{
    float2 g_NoiseTiling;
    float2 g_NoiseScroll;
    float g_Threshold;
    float g_Edge;
    uint g_VertexCount;
    uint g_SourceId;
};

ByteAddressBuffer emitSource : register(t0);
StructuredBuffer<EmitSourceLayout> sourceLayouts : register(t1);
Texture2D noiseTex : register(t2);
SamplerState linearWrap : register(s0);

AppendStructuredBuffer<uint> edgeIndices : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_VertexCount)
        return;

    EmitSourceLayout L = sourceLayouts[g_SourceId];
    if (L.stride == 0u)
        return;

    float2 uv = asfloat(emitSource.Load2(i * L.stride + L.uvOffset));
    float n = noiseTex.SampleLevel(linearWrap, uv * g_NoiseTiling + g_NoiseScroll, 0).r;

    float d = n - g_Threshold;
    if (d >= 0.0 && d < max(g_Edge, 1e-4))
        edgeIndices.Append(i);
}
