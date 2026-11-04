// ============================================
// EdgeFilterCS.hlsl
// ディゾルブの縁に乗っている発生元の頂点のリストを作る。
// 1 頂点に 1 スレッド。ノイズの引き方は VFXMeshPS と同じ：
//   n = noise(uv * tiling + scroll).r
// PS は  n >= threshold  を残し、 n < threshold + edge  を燃える
// 縁として塗るので、ここではその帯を集める。
// 出力は頂点インデックスの AppendStructuredBuffer。呼び出し側がその数を
// EmitCS が読む小さなバッファにコピーする（リードバックなし）。
// ============================================
#include "Common/ParticleCommon.hlsli"

// b0 / b1 は ParticleCommon のもの（ここでは未使用）
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
