// ============================================================
// TextOutlinePS.hlsl
// Text outline for DirectXTK SpriteBatch (custom pixel shader, 2026-10-23,
// damage numbers). The outline is made here by looking around each pixel,
// so an outlined string costs 2 DrawString calls (outline + body) instead of
// 9 (8 offset copies + body).
//   color.a <  0 : outline layer. Coverage = max glyph alpha within
//                  g_OutlinePx screen pixels; output = rgb x coverage,
//                  alpha = -color.a x coverage (rgb is premultiplied)
//   color.a >= 0 : body. Same as SpriteBatch's default (texture x color)
// The font must be baked with empty padding around every glyph
// (Tools/BuildSpriteFont.ps1 -Pad), else the outline is cut at the glyph quad
// or picks up the neighbour glyph in the atlas. g_MaxTexels = padding - 1.
// ============================================================
Texture2D<float4> g_Tex : register(t0);
SamplerState g_Samp : register(s0);

cbuffer TextOutlineCB : register(b0)
{
    float  g_OutlinePx;   // outline radius in screen pixels
    float  g_MaxTexels;   // never look further than this (texels)
    float2 g_Pad;
};

float4 main(float4 color : COLOR0, float2 uv : TEXCOORD0) : SV_Target0
{
    float4 tex = g_Tex.Sample(g_Samp, uv);
    // derivatives taken outside the branch (they are undefined inside it); the taps below use SampleGrad
    float2 duvx = ddx(uv);
    float2 duvy = ddy(uv);

    // body: done before the 16 outline taps (2026-10-25: the body pixels used to sample them too)
    [branch]
    if (color.a >= 0.0)
        return tex * color;

    // screen pixel -> uv, scaled to the outline radius and clamped to the padding
    uint w, h;
    g_Tex.GetDimensions(w, h);
    float2 size = float2(w, h);
    float2 ux = duvx * g_OutlinePx;
    float2 uy = duvy * g_OutlinePx;
    float texels = max(length(ux * size), length(uy * size));
    float k = min(1.0, g_MaxTexels / max(texels, 1e-4));
    ux *= k;
    uy *= k;

    // 8 directions at the full radius and at half of it (thin corners)
    float a = tex.a;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float ang = i * 0.78539816;
        float2 d = cos(ang) * ux + sin(ang) * uy;
        a = max(a, g_Tex.SampleGrad(g_Samp, uv + d, duvx, duvy).a);
        a = max(a, g_Tex.SampleGrad(g_Samp, uv + d * 0.5, duvx, duvy).a);
    }

    return float4(color.rgb * a, -color.a * a);
}
