// ============================================================
// Lighting.hlsli
// Light constant buffer (PS b0, matches C++ LightBuffer) and the
// shading functions every model PS calls.
//
// All functions return LINEAR HDR radiance. No tonemap, no gamma
// here; that happens once in CompositePS.
//
// Ambient model, half-lambert, point lights etc. get added here
// and every model picks them up without touching its own PS.
// ============================================================
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#ifndef MODEL_LIGHT_CB_REG
#define MODEL_LIGHT_CB_REG b0
#endif

struct DirectionalLight
{
    float3 direction;
    float padding1;
    float3 color;
    float intensity;
};

cbuffer LightBuffer : register(MODEL_LIGHT_CB_REG)
{
    DirectionalLight dirLight;
    float3 ambientColor;
    float padding;
    float3 cameraPosition;
    float padding2;
};

static const float PI = 3.14159265359;

// ------------------------------------------------------------
// Normal map (OpenGL convention, G flipped for DX) -> world normal
// ------------------------------------------------------------
float3 PerturbNormal(float3 N, float3 T, float3 normalMapSample)
{
    float3 nm = normalMapSample * 2.0 - 1.0;
    nm.y = -nm.y;
    N = normalize(N);
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(nm, TBN));
}

// ------------------------------------------------------------
// Lambert: (ambient + directional) * albedo
// ------------------------------------------------------------
float3 ShadeLambert(float3 N, float3 albedo)
{
    float3 L = normalize(-dirLight.direction);
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = dirLight.color * dirLight.intensity * NdotL;
    return (ambientColor + diffuse) * albedo;
}

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

float3 ShadePBR(float3 N, float3 V, float3 albedo,
                float metallic, float roughness, float ao)
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
    float3 radiance = dirLight.color * dirLight.intensity;

    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    float3 ambient = ambientColor * albedo * ao;
    return ambient + Lo;
}

#endif