// ============================================================
// SwarmOrbPS.hlsl
// 経験玉の宝石: 自ら光る結晶。線形 HDR で出し、しきい値を超えた分は
// ブルームが拾う。
//
//   本体     : 頂点カラー x g_Emissive。平らな面を太陽で少し陰影付けする
//              （g_Facet）ので、回る宝石がちらちら光る
//   きらめき : 面に乗る鋭い太陽のハイライト
//   リム     : g_RimColor のフレネルの縁
// 全体を Color.a で拡大縮小し（SwarmOrbVS: 吸い寄せられている間は明るい）、
// 他の受光する面と同じくフォグをかける。
//
// b0 = LightBuffer（太陽の向き、カメラ、フォグ）、b1 = こちらの調整値。
// ============================================================
#include "../Common/ModelCommon.hlsli"
#include "../Common/Lighting.hlsli"

// SwarmSystem::OrbShadeCB と一致させること
cbuffer SwarmOrbShadeCB : register(b1)
{
    float g_Emissive;     // 本体の明るさ
    float g_Facet;        // 0 = 平らに光る、1 = 面を太陽で完全に陰影付け
    float g_RimGain;
    float g_RimPower;

    float g_GlintGain;
    float g_GlintPower;
    float2 _shadePad;

    float4 g_RimColor;    // 線形 rgb
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 N = normalize(input.Normal);
    float3 V = normalize(cameraPosition - input.WorldPos);
    float3 L = normalize(-dirLight.direction);

    float wrap = saturate(dot(N, L) * 0.5 + 0.5);
    float3 body = input.Color.rgb * g_Emissive * lerp(1.0 - g_Facet, 1.0, wrap);

    float3 H = normalize(L + V);
    float glint = pow(saturate(dot(N, H)), max(g_GlintPower, 1.0)) * g_GlintGain;

    float rim = pow(1.0 - saturate(dot(N, V)), max(g_RimPower, 0.1)) * g_RimGain;

    float3 c = (body + glint * dirLight.color + rim * g_RimColor.rgb) * input.Color.a;
    return float4(ApplyFog(c, input.WorldPos), 1.0);
}
