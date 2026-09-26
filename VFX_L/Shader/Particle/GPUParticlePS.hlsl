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
};

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

    float a = tex.a * input.color.a;
    clip(a - 0.01);

    // premultiplied texels already carry their alpha in rgb
    float3 rgb = ((g_PremulMask & bit) != 0u)
        ? tex.rgb * input.color.rgb * input.color.a
        : tex.rgb * input.color.rgb * a;

    return float4(rgb, (input.alphaBlend != 0u) ? a : 0.0);
}
