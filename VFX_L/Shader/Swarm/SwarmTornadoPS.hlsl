// ============================================================
// SwarmTornadoPS.hlsl
// The sand tornado's surface (2026-10-19). Streamline look (user: "not cloud noise,
// slanted flow lines"): t0 is a tileable texture of thin vertical streaks
// (Tools/BuildWindStreaks.ps1). The shell's uv.x already twists along the height
// and we add more slant here, so the streaks become diagonal flow lines that
// scroll upward around the column. t1 (soft cloud noise, low frequency) only
// breaks the streaks into patches so the column is not a uniform barber pole.
// The core layer (Tangent.x = 1) is denser, flows faster and is slanted the other
// way a little, which reads as the inner wind. Ends fade through the vertex alpha
// baked into the shell. Not lit, no fog. Premultiplied alpha output
// (RenderStates AlphaBlend = ONE / INV_SRC_ALPHA).
//   t0 = wind streaks (Material albedo slot), t1 = cloud noise (normal slot)
//   Tangent = (layer, vertex heave noise, time) from SwarmTornadoVS
// ============================================================
#include "../Common/ModelCommon.hlsli"

float4 main(PS_INPUT i) : SV_TARGET
{
    float layer = i.Tangent.x;
    float heave = i.Tangent.y;
    float t = i.Tangent.z;
    float2 uv = i.UV;

    // ---- layer 2: sand haze. No streaks, a soft film that is densest at the foot and thins upward,
    //      patchy through the slow cloud noise so it breathes (user 10-19: "sand beyond a point should
    //      roll up into something like mist"). Colour alpha already holds the haze opacity ----
    if (layer > 1.5)
    {
        float c = normalTexture.Sample(samplerState, float2(uv.x * 1.2 + t * 0.05, uv.y * 0.8 - t * 0.35)).r;
        float body = (0.6 + 0.8 * c) * lerp(1.0, 0.35, uv.y);   // foot 1.0 -> top 0.35
        float alpha = saturate(body) * i.Color.a;
        float3 col = i.Color.rgb * 0.78;                        // a touch darker than the streaks (dust, not sunlit sand)
        return float4(col * alpha, alpha);
    }

    // flow: streaks repeat `rep` times around, lean `slant` turns per height, scroll up `up` per second
    float sp = lerp(1.0, 1.5, layer);
    float rep = lerp(1.2, 1.6, layer);   // fewer repeats around = wider lines on screen (user 10-19: thicker)
    float slant = lerp(0.55, 0.85, layer);
    float up = 1.1 * sp;
    float2 suv = float2(uv.x * rep + uv.y * slant + t * 0.10 * sp, uv.y * 0.9 - t * up);
    float s = albedoTexture.Sample(samplerState, suv).r;
    // a second, finer set of lines offset half a turn, half as bright (two speeds = depth)
    float2 suv2 = float2(uv.x * rep * 1.7 + uv.y * (slant + 0.3) + 0.5 - t * 0.06 * sp, uv.y * 1.3 - t * up * 1.35);
    s = max(s, 0.55 * albedoTexture.Sample(samplerState, suv2).r);

    // patches: slow large cloud noise keeps parts of the column clearer than others
    float c = normalTexture.Sample(samplerState, float2(uv.x * 1.0 + t * 0.03, uv.y * 0.7 - t * 0.25 * sp)).r;
    float n = saturate(s * (0.45 + 0.9 * c) * 1.5 + (heave - 0.5) * 0.25);

    // erosion: below the cut nothing, soft band above. The core keeps more
    float cut = lerp(0.22, 0.18, layer);
    float alpha = smoothstep(cut, cut + 0.35, n) * i.Color.a * lerp(0.65, 0.9, layer);

    // sand: bright along the dense streaks, the foot a little darker (dust near the ground)
    float3 col = i.Color.rgb * (0.62 + 0.42 * n) * (0.86 + 0.14 * uv.y);
    return float4(col * alpha, alpha);
}
