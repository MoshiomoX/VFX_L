// ============================================================
// Dissolve.hlsli
// Dissolve for lit model PS (PS.hlsl / PBR_PS.hlsl).
// Same formula as VFXMeshPS: noise(uv) < threshold is clipped, the
// band just above the threshold glows with edgeColor.
//   PS b1 = DissolveCB (C++ Renderer::DissolveCB), t5 = noise.
//   threshold < 0 turns it off (Renderer writes -1 when the entity
//   has no DissolveComponent).
// Include after ModelCommon.hlsli (uses samplerState s0).
// ============================================================
#ifndef DISSOLVE_HLSLI
#define DISSOLVE_HLSLI

#ifndef DISSOLVE_CB_REG
#define DISSOLVE_CB_REG b1
#endif

cbuffer DissolveCB : register(DISSOLVE_CB_REG)
{
    float2 g_DissolveTiling;
    float2 g_DissolveScroll;
    float g_DissolveThreshold; // < 0 : off
    float g_DissolveEdge;
    float2 g_DissolvePad;
    float4 g_DissolveEdgeColor; // rgb = glow color, a = strength (HDR)
};

Texture2D dissolveNoise : register(t5);

// Clips dissolved pixels. Returns the emissive glow to add to the lit color.
float3 ApplyDissolve(float2 uv)
{
    // single exit: fxc warns X4000 when clip() sits between two returns
    float3 glow = float3(0, 0, 0);
    if (g_DissolveThreshold >= 0.0)
    {
        float n = dissolveNoise.Sample(samplerState, uv * g_DissolveTiling + g_DissolveScroll).r;
        float d = n - g_DissolveThreshold;
        clip(d);
        float edge = 1.0 - saturate(d / max(g_DissolveEdge, 1e-4));
        glow = g_DissolveEdgeColor.rgb * (edge * g_DissolveEdgeColor.a);
    }
    return glow;
}

#endif
