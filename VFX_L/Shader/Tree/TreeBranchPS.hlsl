// ============================================================
// TreeBranchPS.hlsl
// 幹・枝の色（縦の筋を少し）を他のモデルと同じ光で（Lighting.hlsli：トゥーン、太陽の影、点光源、霧）。
// TreeCB が b0 を使うので、ライトの cbuffer は b1 に置く。
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "TreeCommon.hlsli"

StructuredBuffer<TreeState> g_States : register(t1);

struct BranchOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 bark : TEXCOORD2;
    nointerpolation uint tree : TEXCOORD3;
};

float4 main(BranchOut i) : SV_TARGET
{
    // tree の bit 24 = 枯れ木
    uint h = HashU32((i.tree & TREE_INDEX_MASK) * 7919u + 13u);
    float3 albedo = (i.tree & TREE_BARE_BIT) != 0u ? g_BareColor : g_BarkColor;
    // 木ごとの明るさのばらつき + 枝の筋
    albedo *= 0.85 + 0.3 * (float) (h & 0xFFu) / 255.0;
    float stripe = sin(i.bark.x * 7.0 + i.world.x * 3.0 + i.world.z * 3.0);
    albedo *= 0.92 + 0.08 * stripe;
    float3 lit = ShadeLambert(normalize(i.normal), albedo, i.world);
    return float4(lit, 1.0);
}
