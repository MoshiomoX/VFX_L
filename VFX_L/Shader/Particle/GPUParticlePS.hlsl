Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

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
};

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

    float4 texColor = g_Texture.Sample(g_Sampler, atlasUV);
    float4 finalColor = texColor * input.color;
    clip(finalColor.a - 0.01);
    return finalColor;
}