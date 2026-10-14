// ============================================================
// BackdropPS.hlsl
// Full-screen backdrop for the title / result screens (2026-10-09).
// Drawn with Sky/SkyVS (full-screen triangle) into the scene HDR RT.
//
// mode 0 (photo): the captured battle frame (SceneSnapshot), zoomed and
//   shifted so its middle sits right of center, dimmed on the left where
//   the text panels go, with a vignette. Output is opaque (alpha 1).
//   Without a photo (hasPhoto 0) a dark blue-violet gradient instead.
// mode 1 (scrim): no texture. Black with an alpha that is strong on the
//   left and fades out to the right (and at the bottom), drawn with a
//   premultiplied alpha blend over the live title scene.
// ============================================================

Texture2D g_Photo : register(t0);
SamplerState g_Linear : register(s0);

cbuffer BackdropCB : register(b0)
{
    float2 photoCenter;   // where the middle of the photo goes on screen (0..1)
    float  zoom;          // part of the photo shown across the screen (0.8 = 80 %)
    float  brightness;
    float  shadeStrength; // how dark the left side gets (0..1)
    float  shadeFrom;     // screen x (0..1): darkest left of this
    float  shadeTo;       // screen x (0..1): no shade right of this
    float  vignette;
    float  desaturate;
    float  bottomShade;   // extra darkening toward the bottom edge (prompts)
    float  mode;          // 0 photo, 1 scrim
    float  hasPhoto;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float4 main(PSIn i) : SV_TARGET
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;   // screen 0..1, y down

    // left side (and bottom) darkening shared by both modes
    float side = 1.0 - smoothstep(shadeFrom, shadeTo, uv.x);
    float bottom = smoothstep(0.70, 1.0, uv.y) * bottomShade;
    float shade = saturate(side * shadeStrength + bottom);

    if (mode > 0.5)
        return float4(0.0, 0.0, 0.0, shade);   // premultiplied black

    float3 c;
    if (hasPhoto > 0.5)
    {
        float2 src = (uv - photoCenter) * zoom + 0.5;
        c = g_Photo.SampleLevel(g_Linear, src, 0).rgb;
        float l = dot(c, float3(0.2126, 0.7152, 0.0722));
        c = lerp(c, l.xxx, desaturate) * brightness;
    }
    else
    {
        // no photo: deep night-sky gradient (linear values)
        float t = saturate(uv.y);
        c = lerp(float3(0.010, 0.012, 0.030), float3(0.030, 0.020, 0.045), t);
    }

    float2 d = uv - 0.5;
    float vig = 1.0 - vignette * saturate(dot(d, d) * 2.2);
    c *= vig * (1.0 - shade);
    return float4(c, 1.0);
}
