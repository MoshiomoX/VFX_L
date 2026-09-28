// ============================================================
// SkyPS.hlsl
// Gradient sky behind everything (drawn first, no depth). The view
// ray comes from the inverse view-projection; its elevation picks
// the color:
//   above the horizon : horizon -> zenith (pow curve, most of the
//                       sky stays close to the horizon tint)
//   below the horizon : horizon -> below (hidden by the ground on the
//                       battle map, visible past the edge walls)
// plus a soft glow and a small disk toward the sun.
// Output is linear HDR like every scene shader (CompositePS applies
// exposure / tonemap / gamma). The horizon color doubles as the
// distance fog color, so far terrain melts into the sky.
// C++ mirror: SkyRenderer::SkyCB (row_major, no transpose)
// ============================================================
cbuffer SkyCB : register(b0)
{
    row_major float4x4 g_InvViewProj;
    float3 g_CameraPos;
    float g_ZenithCurve;   // exponent on the elevation (< 1 = the horizon band is wider)
    float3 g_Zenith;
    float g_SunGlow;       // glow strength
    float3 g_Horizon;
    float g_SunGlowPower;  // glow tightness (bigger = smaller halo)
    float3 g_Below;
    float g_SunDisk;       // disk strength (0 = none)
    float3 g_SunDir;       // direction the light travels (from the sun)
    float _skyPad0;
    float3 g_SunColor;
    float _skyPad1;
};

struct SkyIn
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float4 main(SkyIn i) : SV_TARGET
{
    float4 far = mul(float4(i.ndc, 1.0, 1.0), g_InvViewProj);
    float3 dir = normalize(far.xyz / far.w - g_CameraPos);

    float up = dir.y;
    float3 c = (up >= 0.0)
        ? lerp(g_Horizon, g_Zenith, pow(saturate(up), g_ZenithCurve))
        : lerp(g_Horizon, g_Below, saturate(-up * 4.0));

    float toSun = saturate(dot(dir, -g_SunDir));
    c += g_SunColor * (g_SunGlow * pow(toSun, g_SunGlowPower));
    c += g_SunColor * (g_SunDisk * smoothstep(0.9990, 0.9995, toSun));
    return float4(c, 1.0);
}
