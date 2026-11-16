// ============================================================
// TreeLeafPS.hlsl
// 板から葉の形（先のとがった楕円 + 中央脈）を切り抜き、他のモデルと同じ光で。
// 色 = 木ごとに g_LeafColor 〜 g_LeafColor2、樹冠の奥ほど暗く。不透明で深度を書く。
// TreeCB が b0 を使うので、ライトの cbuffer は b1 に置く。
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "TreeCommon.hlsli"
#include "TreeLeafShape.hlsli"

struct LeafOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint seed : TEXCOORD3;
    nointerpolation float shade : TEXCOORD4;
};

float4 main(LeafOut i) : SV_TARGET
{
    float w = TreeLeafWidth(i.uv.x);
    float y = abs(i.uv.y);
    if (y > w)
        discard;

    float tint = (float) (i.seed >> 24) / 255.0;
    float v = (float) ((i.seed >> 4) & 0xFFu) / 255.0;
    float3 albedo = lerp(g_LeafColor, g_LeafColor2, tint) * (0.88 + 0.24 * v);
    albedo *= lerp(g_InnerDark, 1.0, i.shade);
    if (y < 0.1 * w + 0.03)
        albedo *= 0.86;   // 中央脈

    float3 lit = ShadeLambert(normalize(i.normal), albedo, i.world);
    return float4(lit, 1.0);
}
