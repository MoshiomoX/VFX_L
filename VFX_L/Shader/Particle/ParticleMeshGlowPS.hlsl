// ============================================
// ParticleMeshGlowPS.hlsl
// Glow pass of mesh particles (additive, unlit, depth read-only).
// Colour = model albedo x particle colour; the particle's colour alpha
// (colour over lifetime) fades it out. The blend state is
// SRC_ALPHA / ONE, so the colour is not premultiplied here.
// Pairs with ParticleMeshVS.
// ============================================
#include "../Common/ModelCommon.hlsli"

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 tex = albedoTexture.Sample(samplerState, input.UV);
    return tex * input.Color;
}
