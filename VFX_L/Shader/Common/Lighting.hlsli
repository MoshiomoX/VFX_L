// ============================================================
// Lighting.hlsli
// Light constant buffer (PS b0, matches C++ LightBuffer) and the
// shading functions every model PS calls.
//
// All functions return LINEAR HDR radiance. No tonemap, no gamma
// here; that happens once in CompositePS.
//
// Light model (close to Unity's default scene light):
//   hemisphere ambient : sky color on up-facing surfaces, ground
//                        color on down-facing ones. SetAmbientColor
//                        puts the same color in both (= flat ambient)
//   directional light  : diffuse + GGX specular
//   point lights       : distance falloff, diffuse + GGX specular
//
// New light terms get added here and every model picks them up
// without touching its own PS.
// ============================================================
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#ifndef MODEL_LIGHT_CB_REG
#define MODEL_LIGHT_CB_REG b0
#endif

#include "PointLights.hlsli"

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
    float3 ambientColor;        // hemisphere top (sky)
    float padding;
    float3 cameraPosition;
    float padding2;
    float3 groundAmbientColor;  // hemisphere bottom (ground)
    float padding3;
    float3 fogColor;            // distance fog (ApplyFog). linear HDR, = the sky's horizon
    float fogStart;             // meters from the camera where it starts
    float fogEnd;               // meters where it reaches fogMax
    float fogMax;               // 0 = no fog (default outside the battle scene)
    float albedoSrgb;           // 1 = albedo textures hold sRGB: decode before shading (DecodeAlbedo)
    float fogPad;
};

// ------------------------------------------------------------
// Albedo textures are loaded as UNORM, so an sRGB image arrives
// gamma-encoded. With albedoSrgb on it is brought back to linear
// before lighting (CompositePS re-applies 1/2.2 at the end);
// off keeps the old, lighter look
// ------------------------------------------------------------
float3 DecodeAlbedo(float3 c)
{
    return (albedoSrgb > 0.5) ? pow(max(c, 0.0), 2.2) : c;
}

static const float PI = 3.14159265359;

// Lambert materials carry no roughness / metallic. For their
// highlights they are treated as a plain dielectric (F0 0.04) with
// this roughness (about Unity Standard's default smoothness 0.5)
static const float LAMBERT_SPEC_ROUGHNESS = 0.45;
static const float3 DIELECTRIC_F0 = float3(0.04, 0.04, 0.04);

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
// Distance fog: lit color -> fogColor between fogStart and fogEnd
// (smoothstep, capped at fogMax). Far terrain melts into the sky.
// Applied at the end of the world-position shade functions below
// ------------------------------------------------------------
float3 ApplyFog(float3 color, float3 worldPos)
{
    float d = distance(worldPos, cameraPosition);
    float f = smoothstep(fogStart, max(fogEnd, fogStart + 1e-3), d) * fogMax;
    return lerp(color, fogColor, f);
}

// ------------------------------------------------------------
// Hemisphere ambient: sky from above, ground from below, blended by
// how much the surface faces up
// ------------------------------------------------------------
float3 AmbientAt(float3 N)
{
    return lerp(groundAmbientColor, ambientColor, saturate(N.y * 0.5 + 0.5));
}

// ------------------------------------------------------------
// Lambert without a view vector (no specular):
// (ambient + directional) * albedo
// ------------------------------------------------------------
float3 ShadeLambert(float3 N, float3 albedo)
{
    float3 L = normalize(-dirLight.direction);
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = dirLight.color * dirLight.intensity * NdotL;
    return (AmbientAt(N) + diffuse) * albedo;
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

// specular BRDF only (caller multiplies light color and N.L)
float3 SpecularGGX(float3 N, float3 V, float3 L, float roughness, float3 F0)
{
    float3 H = normalize(V + L);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    return (D * G * F) / max(denom, 1e-4);
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
    float3 ambient = AmbientAt(N) * albedo * ao;
    return ambient + Lo;
}

// ------------------------------------------------------------
// Point lights (PointLights.hlsli list): diffuse + GGX specular in
// one loop.
//   diffuse  : sum of radiance * N.L   (caller multiplies the albedo)
//   specular : sum of GGX lobe * radiance * N.L   (final)
// ------------------------------------------------------------
void PointLightShade(float3 worldPos, float3 N, float3 V,
                     float roughness, float3 F0,
                     out float3 diffuse, out float3 specular)
{
    diffuse = float3(0, 0, 0);
    specular = float3(0, 0, 0);

    uint count = min(g_PointLightCount[0], MAX_POINT_LIGHTS);
    for (uint i = 0u; i < count; ++i)
    {
        PointLight l = g_PointLights[i];
        float3 d = l.position - worldPos;
        float dist = length(d);
        if (dist >= l.radius)
            continue;
        float3 L = d / max(dist, 1e-4);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0)
            continue;

        float3 radiance = l.color * (l.intensity * PointLightAttenuation(dist, l.radius));
        diffuse += radiance * NdotL;
        specular += SpecularGGX(N, V, L, roughness, F0) * radiance * NdotL;
    }
}

// ------------------------------------------------------------
// Lambert + directional highlight + point lights (needs the world
// position for the view vector and the point lights)
// ------------------------------------------------------------
float3 ShadeLambert(float3 N, float3 albedo, float3 worldPos)
{
    float3 V = normalize(cameraPosition - worldPos);

    // directional: same diffuse as the view-less version, plus highlight
    float3 L = normalize(-dirLight.direction);
    float NdotL = max(dot(N, L), 0.0);
    float3 sun = dirLight.color * (dirLight.intensity * NdotL);
    float3 sunSpec = SpecularGGX(N, V, L, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0) * sun;

    float3 diff, spec;
    PointLightShade(worldPos, N, V, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0, diff, spec);

    // this Lambert has no 1/PI (the light intensity absorbs it), so the
    // specular lobes get the same PI to keep their ratio to the diffuse
    float3 lit = (AmbientAt(N) + sun + diff) * albedo + (sunSpec + spec) * PI;
    return ApplyFog(lit, worldPos);
}

// PBR + point lights: same split as the directional term above
float3 ShadePBR(float3 N, float3 V, float3 albedo,
                float metallic, float roughness, float ao, float3 worldPos)
{
    float3 base = ShadePBR(N, V, albedo, metallic, roughness, ao);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 diff, spec;
    PointLightShade(worldPos, N, V, roughness, F0, diff, spec);
    float3 kD = (1.0 - metallic) * albedo / PI;
    return ApplyFog(base + diff * kD + spec, worldPos);
}

#endif
