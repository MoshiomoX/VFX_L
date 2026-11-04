// ============================================================
// GrassPS.hlsl
// 地面の色を根元で暗く、先端で明るく暖かくし、
// 他のモデルと同じようにライティングする（Lighting.hlsli: 半球環境光、影付きの
// 太陽、点光源、フォグ）。b0 は GrassCB が使うので、ここではライトの
// cbuffer を b1 に置く。
// 2026-10-23: 枯れた葉（先端ほど黄色い）と、突風で裏返った / 風源で
// 巻き上げられた草の葉のつや（明るく、暖かく）。
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "GrassCommon.hlsli"

struct GrassOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : TEXCOORD2;
    float t : TEXCOORD3;
    float glow : TEXCOORD4;
    float sheen : TEXCOORD5;
    float dry : TEXCOORD6;
};

float4 main(GrassOut i) : SV_TARGET
{
    float3 albedo = i.color * lerp(g_RootColor, g_TipColor, i.t);
    albedo *= lerp(float3(1.0, 1.0, 1.0), g_DryColor.rgb, saturate(i.dry) * (0.4 + 0.6 * i.t));
    // 燃える区画（dry 1..2、GrassFireField）: 根元から先端まで藁色にし、高いカメラからでも分かるようにする。
    // 葉の明るさは保つので、焼けた草は黒いまま
    float strawPatch = saturate(i.dry - 1.0);
    if (strawPatch > 0.0)
        albedo = lerp(albedo, float3(1.3, 0.92, 0.3) * dot(albedo, float3(0.3, 0.6, 0.1)) * 1.25, strawPatch * 0.9);
    albedo = lerp(albedo, albedo * g_Sheen.rgb, saturate(i.sheen * g_Sheen.a * (0.35 + 0.65 * i.t)));
    float3 lit = ShadeLambert(normalize(i.normal), albedo, i.world);
    // 燃えている草の残り火の光: オレンジ、先端ほど明るい（HDR -> ブルーム）
    lit += float3(1.0, 0.36, 0.05) * (i.glow * g_FireGlow * (0.35 + 0.65 * i.t));
    return float4(lit, 1.0);
}
