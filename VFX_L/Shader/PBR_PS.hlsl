// ============================================================
// PBR_PS.hlsl
// Cook-Torrance BRDF（物理ベース）
//   t0=Albedo, t1=Normal(OpenGL), t2=Metallic, t3=Roughness, t4=AO
//   ※欠落テクスチャは CPU 側(Material)で既定テクスチャを bind 済み前提：
//     Albedo=白 / Normal=(128,128,255) / Metallic=黒 / Roughness=白 / AO=白
// ============================================================
Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicTexture : register(t2);
Texture2D roughnessTexture : register(t3);
Texture2D aoTexture : register(t4);
SamplerState samplerState : register(s0);

struct DirectionalLight
{
    float3 direction;
    float padding1;
    float3 color;
    float intensity;
};

cbuffer LightBuffer : register(b0)
{
    DirectionalLight dirLight;
    float3 ambientColor;
    float padding;
    float3 cameraPosition;
    float padding2;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

static const float PI = 3.14159265359;

// --- GGX 法線分布 ---
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

// --- 幾何項（Smith / Schlick-GGX）---
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

// --- フレネル（Schlick）---
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float4 main(PS_INPUT input) : SV_TARGET
{
    // --- テクスチャサンプル（欠落分は既定テクスチャがbind済み）---
    float3 albedo = albedoTexture.Sample(samplerState, input.UV).rgb;
    float metallic = metallicTexture.Sample(samplerState, input.UV).r;
    float roughness = roughnessTexture.Sample(samplerState, input.UV).r;
    float ao = aoTexture.Sample(samplerState, input.UV).r;

    // --- 法線マップ（OpenGL → DirectX：G 反転）---
    float3 normalMap = normalTexture.Sample(samplerState, input.UV).rgb;
    normalMap = normalMap * 2.0 - 1.0;
    normalMap.y = -normalMap.y; // OpenGL → DX

    // --- TBN ---
    float3 N = normalize(input.Normal);
    float3 T = normalize(input.Tangent);
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt 直交化
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    N = normalize(mul(normalMap, TBN));

    // --- ベクトル ---
    float3 V = normalize(cameraPosition - input.WorldPos);
    float3 L = normalize(-dirLight.direction);
    float3 H = normalize(V + L);

    // --- F0（非金属0.04、金属はalbedo）---
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // --- Cook-Torrance BRDF ---
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = numerator / max(denom, 0.0001);

    // --- 拡散（金属は拡散しない）---
    float3 kS = F;
    float3 kD = (float3(1, 1, 1) - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    float3 radiance = dirLight.color * dirLight.intensity;

    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // --- 環境光（AO で減衰）---
    float3 ambient = ambientColor * albedo * ao;

    float3 color = ambient + Lo;

    // --- 簡易トーンマッピング + ガンマ ---
    color = color / (color + float3(1, 1, 1)); // Reinhard
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2)); // gamma

    return float4(color * input.Color.rgb, 1.0);
}