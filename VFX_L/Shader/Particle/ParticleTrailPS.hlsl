// ============================================================
// ParticleTrailPS.hlsl
// リボントレイルのピクセルシェーダ。テクスチャ × 頂点色、幅方向の
// ソフトエッジは任意。
//   U はリボンに沿う（先頭 -> 尾）、V は幅方向。
//   g_Premultiply = 1 はアルファブレンドのパス（ONE / INV_SRC_ALPHA）、
//   0 は加算のパス（SRC_ALPHA / ONE）。
// ============================================================
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer TrailDrawCB : register(b1)
{
    uint g_StyleSlot;
    uint g_Premultiply;
    float g_Time;
    float _padT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation float softEdge : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 c = g_Texture.Sample(g_Sampler, input.uv) * input.color;

    // 中心線で 1、両端で 0
    float centre = 1.0 - abs(input.uv.y * 2.0 - 1.0);
    if (input.softEdge > 0.001)
        c.a *= saturate(centre / input.softEdge);

    clip(c.a - 0.003);

    if (g_Premultiply != 0u)
        c.rgb *= c.a;
    return c;
}
