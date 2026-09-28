// ============================================================
// SwarmBlobShadowPS.hlsl
// Darkens the ground under a mob: black with a soft round edge.
// Premultiplied alpha (the blend is ONE / INV_SRC_ALPHA), so rgb = 0
// and the result is dst * (1 - a).
// ============================================================
cbuffer SwarmBlobShadowCB : register(b0)
{
    row_major float4x4 g_BlobView;
    row_major float4x4 g_BlobProj;
    float g_BlobRadius;
    float g_BlobStrength;
    float g_BlobLift;
    float g_BlobSoftness;
    float g_BlobClamp;
    float3 _blobPad;
};

struct BlobOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(BlobOut i) : SV_TARGET
{
    float d = length(i.uv);
    float inner = 1.0 - saturate(g_BlobSoftness);
    float a = g_BlobStrength * (1.0 - smoothstep(inner, 1.0, d));
    clip(a - 0.001);
    return float4(0.0, 0.0, 0.0, a);
}
