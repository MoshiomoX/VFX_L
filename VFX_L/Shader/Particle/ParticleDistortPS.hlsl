// ============================================================
// ParticleDistortPS.hlsl
// Screen distortion pass of the billboard particles (2026-10-20, the Elden
// Ring style wind: the air itself bends the picture). GPUParticleVS with
// g_Pass 3 feeds only the particles that carry a distortion level; this PS
// writes an offset (in uv units, before the composite's strength) into the
// R16G16 target: the shape (texture alpha x colour alpha, the VS folded the
// level into the alpha) pushes the picture outward from the quad's centre.
// Added with ONE / ONE so overlapping puffs sum. The composite
// (CompositePS) samples the scene at uv + offset x strength.
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

// same layout as GPUParticlePS (ParticleSheetCB)
cbuffer ParticleSheetCB : register(b0)
{
    uint g_PointMask;
    uint g_PremulMask;
    uint2 _pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation uint atlasRows : TEXCOORD1;
    nointerpolation uint atlasCols : TEXCOORD2;
    nointerpolation uint uvFrame : TEXCOORD3;
    nointerpolation uint sheet : TEXCOORD4;
    nointerpolation uint alphaBlend : TEXCOORD5;
    nointerpolation uint renderMode : TEXCOORD6;
    nointerpolation float2 toonFade : TEXCOORD7;
    nointerpolation uint solidPass : TEXCOORD8;
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

// the offset scale of one fully opaque texel at level 3 (uv units; the composite multiplies its strength on top)
static const float DISTORT_UV = 0.02;

float4 main(PSInput input) : SV_TARGET
{
    uint cols = max(input.atlasCols, 1u);
    uint rows = max(input.atlasRows, 1u);
    float cellW = 1.0 / (float) cols;
    float cellH = 1.0 / (float) rows;
    uint col = input.uvFrame % cols;
    uint row = input.uvFrame / cols;
    float2 atlasUV = float2(input.uv.x * cellW + (float) col * cellW,
                            input.uv.y * cellH + (float) row * cellH);
    float2 dx = ddx(atlasUV);
    float2 dy = ddy(atlasUV);

    uint sheet = min(input.sheet, 7u);
    uint bit = 1u << sheet;
    float4 tex;
    [branch]
    if ((g_PointMask & bit) != 0u)
        tex = SampleSheet(sheet, g_Point, atlasUV, dx, dy);
    else
        tex = SampleSheet(sheet, g_Linear, atlasUV, dx, dy);

    float shape = tex.a * input.color.a;
    clip(shape - 0.005);

    // outward from the quad's centre (a bulge); the direction fades to nothing at the centre
    float2 fromCentre = (input.uv - 0.5) * 2.0;
    float2 offset = fromCentre * shape * DISTORT_UV;
    return float4(offset, 0.0, 1.0);
}
