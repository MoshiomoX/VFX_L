// ============================================
// ParticleMeshGlowPS.hlsl
// Glow pass of mesh particles (additive, unlit, depth read-only).
// Colour = model albedo x particle colour; the particle's colour alpha
// (colour over lifetime) fades it out. The blend state is
// SRC_ALPHA / ONE, so the colour is not premultiplied here.
// Pairs with ParticleMeshVS.
// "Behind" (PARTICLE_MESH_BEHIND, Tangent.x = 1 from the VS): only the back
// faces are drawn, so the glow sits behind the opaque body and shows as a
// rim around it. The glow pass rasterizes with cull none, so the facing is
// decided here.
// ============================================
#include "../Common/ModelCommon.hlsli"

float4 main(PS_INPUT input, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    if (input.Tangent.x > 0.5 && isFront)
        discard;
    float4 tex = albedoTexture.Sample(samplerState, input.UV);
    return tex * input.Color;
}
