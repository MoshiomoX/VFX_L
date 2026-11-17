// ============================================================
// LightingPBR.hlsli
// Cook-Torrance GGX の部品と、平行光源 + 半球環境光の PBR（ShadePBRSun / ShadePBR）。
// Lighting.hlsli の中から include する（AmbientAt、dirLight を使う）。
// ============================================================
#ifndef LIGHTING_PBR_HLSLI
#define LIGHTING_PBR_HLSLI
#include "MathConst.hlsli"

// ------------------------------------------------------------
// Cook-Torrance GGX
// ------------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// 鏡面反射の BRDF だけ（ライトの色と N.L は呼び出し側で掛ける）
float3 SpecularGGX(float3 N, float3 V, float3 L, float roughness, float3 F0)
{
    float3 H = normalize(V + L);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    return (D * G * F) / max(denom, 1e-4);
}

// ルックアップテクスチャを使わない split-sum の環境 BRDF（Karis「Physically Based Shading on Mobile」、
// 元は Lazarov）: この角度と粗さで、面が環境光のうちどれだけを鏡面反射するか
float3 EnvBRDFApprox(float3 F0, float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

// sunScale: 平行光源のうち面に届く割合（影）
float3 ShadePBRSun(float3 N, float3 V, float3 albedo,
                   float metallic, float roughness, float ao, float sunScale)
{
    float3 L = normalize(-dirLight.direction);
    float3 H = normalize(V + L);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = (NDF * G * F) / max(denom, 0.0001);

    float3 kD = (float3(1, 1, 1) - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    float3 radiance = dirLight.color * (dirLight.intensity * sunScale);

    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // エネルギー保存した環境光:
    //   鏡面 = 反射方向に見える半球環境光 x 環境 BRDF
    //   拡散 = 鏡面が取らなかった残り。金属では 0
    // （IBL のキューブマップはない。半球の色をプリフィルタ済みの環境の代わりにする）
    float NdotV = max(dot(N, V), 1e-4);
    float3 envSpec = EnvBRDFApprox(F0, roughness, NdotV);
    float3 R = reflect(-V, N);
    float3 ambientSpec = AmbientAt(R) * envSpec;
    float3 ambientDiff = AmbientAt(N) * albedo * (float3(1, 1, 1) - envSpec) * (1.0 - metallic);
    float3 ambient = (ambientDiff + ambientSpec) * ao;
    return ambient + Lo;
}

float3 ShadePBR(float3 N, float3 V, float3 albedo,
                float metallic, float roughness, float ao)
{
    return ShadePBRSun(N, V, albedo, metallic, roughness, ao, 1.0);
}

#endif
