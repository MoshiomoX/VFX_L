// ============================================================
// GPUParticlePS.hlsl
// Billboard particles. One draw for every particle, blended with
// premultiplied alpha (ONE / INV_SRC_ALPHA):
//   additive particles write alpha 0  -> dst + rgb*a (same as the old SRC_ALPHA/ONE)
//   alpha particles write alpha a     -> dst*(1-a) + rgb*a
// The texture is picked per particle from the ParticleSheets table
// (t0..t7). SM5.0 cannot index a texture array with a dynamic index,
// hence the switch. The branch is uniform per particle (a quad never
// spans two primitives), and gradients are taken before it.
// ============================================================

Texture2D g_Sheet0 : register(t0);
Texture2D g_Sheet1 : register(t1);
Texture2D g_Sheet2 : register(t2);
Texture2D g_Sheet3 : register(t3);
Texture2D g_Sheet4 : register(t4);
Texture2D g_Sheet5 : register(t5);
Texture2D g_Sheet6 : register(t6);
Texture2D g_Sheet7 : register(t7);

SamplerState g_Linear : register(s0);
SamplerState g_Point : register(s1);

// bit i = sheet i. Written by GPUParticleSystem::Render from the manifests
cbuffer ParticleSheetCB : register(b0)
{
    uint g_PointMask;   // sample with the point sampler (pixel art)
    uint g_PremulMask;  // texels are stored premultiplied
    uint2 _pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    // uint: signed integer divide/modulo on GPU needs emulation
    // instructions for sign handling (fxc warning X3556).
    // These are never negative, so uint is safe and faster.
    nointerpolation uint atlasRows : TEXCOORD1;
    nointerpolation uint atlasCols : TEXCOORD2;
    nointerpolation uint uvFrame : TEXCOORD3;
    nointerpolation uint sheet : TEXCOORD4;
    nointerpolation uint alphaBlend : TEXCOORD5;
    nointerpolation uint renderMode : TEXCOORD6;
    nointerpolation float2 toonFade : TEXCOORD7;   // x = life left (erosion), y = opacity
};

// ------------------------------------------------------------
// Toon billboards (2026-10-04). Bits of renderMode, must match
// ParticleRenderMode::PackToon (GPUParticle.h):
//   bit 14 = toon, bit 15 = outline, bits 16-19 = alpha cut (0.05 + 0.05 n),
//   bits 20-21 = color bands - 1, bits 22-25 = outer band brightness (n / 15)
// ------------------------------------------------------------
#define PARTICLE_TOON         (1u << 14)
#define PARTICLE_TOON_OUTLINE (1u << 15)
static const float TOON_OUTLINE_WIDTH = 0.07;   // alpha units inside the cut
static const float TOON_OUTLINE_DARK = 0.22;    // the line = color x this
static const float TOON_SHAPE_BLUR = 8.0;       // gradient scale for the shape sample (about 3 mips up)

// texRgb: the sampled texel, straight color (already divided by its alpha)
// ta    : the texture alpha (the shape), pc: the particle color
// fade  : x = life left relative to the start alpha, y = opacity (from the VS)
// aa    : fwidth(ta), taken by the caller outside the branch
float4 ToonBillboard(float3 texRgb, float ta, float aa, float4 pc, float2 fade, uint mode, bool alphaBlend)
{
    float cut = 0.05 + 0.05 * (float)((mode >> 16u) & 15u);
    uint bands = 1u + min((mode >> 20u) & 3u, 2u);
    float shade = (float)((mode >> 22u) & 15u) / 15.0;

    // the life fade erodes the shape (rising cut) instead of fading it out
    float thr = lerp(1.0, cut, fade.x);
    aa = max(aa, 1e-3);
    float mask = smoothstep(thr - aa, thr + aa, ta);
    if (mask <= 0.0)
        discard;

    // bands from the edge inward: outermost = shade, innermost = 1
    float inner = saturate((ta - thr) / max(1.0 - thr, 1e-3));   // 0 at the edge .. 1 at the core
    float level = 1.0;
    if (bands > 1u)
    {
        float steps = (float)(bands - 1u);
        float b = 0.0;
        [unroll]
        for (uint k = 1u; k < 3u; ++k)
        {
            if (k < bands)
            {
                float edgeK = (float)k / (float)bands;
                b += smoothstep(edgeK - aa * 2.0, edgeK + aa * 2.0, inner);
            }
        }
        level = lerp(shade, 1.0, b / steps);
    }
    float3 rgb = texRgb * pc.rgb * level;

    // a dark rim just inside the edge
    if (mode & PARTICLE_TOON_OUTLINE)
    {
        float lineW = TOON_OUTLINE_WIDTH * (1.0 - thr) / max(1.0 - cut, 1e-3);   // thins out as it erodes
        float rim = 1.0 - smoothstep(thr + lineW - aa, thr + lineW + aa, ta);   // ('line' is a reserved word)
        rgb = lerp(rgb, texRgb * pc.rgb * TOON_OUTLINE_DARK, rim);
    }

    // premultiplied output: alpha-blended = a crisp shape at the start opacity, additive = alpha 0
    float a = mask * (alphaBlend ? fade.y : 1.0);
    return float4(rgb * a, alphaBlend ? a : 0.0);
}

float4 SampleSheet(uint sheet, SamplerState s, float2 uv, float2 dx, float2 dy)
{
    switch (sheet)
    {
        case 0: return g_Sheet0.SampleGrad(s, uv, dx, dy);
        case 1: return g_Sheet1.SampleGrad(s, uv, dx, dy);
        case 2: return g_Sheet2.SampleGrad(s, uv, dx, dy);
        case 3: return g_Sheet3.SampleGrad(s, uv, dx, dy);
        case 4: return g_Sheet4.SampleGrad(s, uv, dx, dy);
        case 5: return g_Sheet5.SampleGrad(s, uv, dx, dy);
        case 6: return g_Sheet6.SampleGrad(s, uv, dx, dy);
        default: return g_Sheet7.SampleGrad(s, uv, dx, dy);
    }
}

float4 main(PSInput input) : SV_TARGET
{
    // Atlas UV.
    // Guard against 0: emitters that do not use an atlas leave
    // atlasRows/Cols at 0, and 1.0/0 plus "% 0" are undefined.
    // Treat 0 as a single 1x1 cell.
    uint cols = max(input.atlasCols, 1u);
    uint rows = max(input.atlasRows, 1u);

    float cellW = 1.0 / (float) cols;
    float cellH = 1.0 / (float) rows;

    uint col = input.uvFrame % cols;
    uint row = input.uvFrame / cols;

    float2 atlasUV = float2(
        input.uv.x * cellW + (float) col * cellW,
        input.uv.y * cellH + (float) row * cellH
    );

    float2 dx = ddx(atlasUV);
    float2 dy = ddy(atlasUV);

    uint sheet = min(input.sheet, 7u);
    uint bit = 1u << sheet;
    // if/else, not ?: (HLSL evaluates both sides of ?:, that would sample twice)
    float4 tex;
    [branch]
    if ((g_PointMask & bit) != 0u)
        tex = SampleSheet(sheet, g_Point, atlasUV, dx, dy);
    else
        tex = SampleSheet(sheet, g_Linear, atlasUV, dx, dy);

    // toon: crisp shape, color bands, dark rim (per emitter, PARTICLE_TOON).
    // The shape comes from a blurred copy of the texel (a higher mip via scaled
    // gradients): noisy smoke textures otherwise threshold into lace. The alpha
    // gradient is taken after the branch, outside flow control
    float4 soft = tex;
    [branch]
    if (input.renderMode & PARTICLE_TOON)
        soft = SampleSheet(sheet, g_Linear, atlasUV, dx * TOON_SHAPE_BLUR, dy * TOON_SHAPE_BLUR);
    float alphaWidth = fwidth(soft.a);
    if (input.renderMode & PARTICLE_TOON)
    {
        float3 straight = ((g_PremulMask & bit) != 0u) ? soft.rgb / max(soft.a, 1e-3) : soft.rgb;
        return ToonBillboard(straight, soft.a, alphaWidth, input.color, input.toonFade, input.renderMode, input.alphaBlend != 0u);
    }

    float a = tex.a * input.color.a;
    clip(a - 0.01);

    // premultiplied texels already carry their alpha in rgb
    float3 rgb = ((g_PremulMask & bit) != 0u)
        ? tex.rgb * input.color.rgb * input.color.a
        : tex.rgb * input.color.rgb * a;

    return float4(rgb, (input.alphaBlend != 0u) ? a : 0.0);
}
