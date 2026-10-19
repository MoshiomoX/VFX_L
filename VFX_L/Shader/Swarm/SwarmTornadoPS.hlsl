// ============================================================
// SwarmTornadoPS.hlsl
// The sand tornado's surface (2026-10-19). The usual real-time recipe
// (80.lv "Creating a tornado in Unity" and the like): two tiling cloud
// noises scroll up the shell's spiral uv at different speeds, their
// product is eroded by a soft threshold into streaks of sand, the ends
// fade through the vertex alpha baked into the shell. The core layer
// (Tangent.x = 1) scrolls faster and is denser than the outer veil.
// Not lit (sand against the sky reads fine flat), no fog. Output is
// premultiplied alpha (RenderStates AlphaBlend = ONE / INV_SRC_ALPHA).
//   t0 = cloud noise A (Material albedo slot), t1 = cloud noise B (normal slot)
//   Tangent = (layer, vertex heave noise, time) from SwarmTornadoVS
// ============================================================
#include "../Common/ModelCommon.hlsli"

float4 main(PS_INPUT i) : SV_TARGET
{
    float layer = i.Tangent.x;
    float heave = i.Tangent.y;
    float t = i.Tangent.z;
    float2 uv = i.UV;

    // the core flows faster; uv.y grows upward, so subtracting time scrolls the pattern up
    float sp = lerp(1.0, 1.6, layer);
    float a = albedoTexture.Sample(samplerState, float2(uv.x * 2.0 + t * 0.12 * sp, uv.y * 1.5 - t * 0.75 * sp)).r;
    float b = normalTexture.Sample(samplerState, float2(uv.x * 3.0 - t * 0.08 * sp, uv.y * 2.2 - t * 1.25 * sp)).r;
    float n = saturate(a * b * 2.4 + (heave - 0.5) * 0.3);

    // erosion: below the cut nothing, a soft band above it. The core keeps more
    float cut = lerp(0.30, 0.26, layer);
    float alpha = smoothstep(cut, cut + 0.40, n) * i.Color.a * lerp(0.6, 0.9, layer);

    // sand: brighter where the noise is dense, the foot a little darker (dust near the ground)
    float3 col = i.Color.rgb * (0.7 + 0.5 * n) * (0.86 + 0.14 * uv.y);
    return float4(col * alpha, alpha);
}
