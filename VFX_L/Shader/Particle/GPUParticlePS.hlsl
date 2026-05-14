Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation int atlasRows : TEXCOORD1;
    nointerpolation int atlasCols : TEXCOORD2;
    nointerpolation int uvFrame : TEXCOORD3;
};

float4 main(PSInput input) : SV_TARGET
{
    // アトラスUV計算
    float cellW = 1.0 / input.atlasCols;
    float cellH = 1.0 / input.atlasRows;
    int col = input.uvFrame % input.atlasCols;
    int row = input.uvFrame / input.atlasCols;
    float2 atlasUV = float2(
        input.uv.x * cellW + col * cellW,
        input.uv.y * cellH + row * cellH
    );

    float4 texColor = g_Texture.Sample(g_Sampler, atlasUV);
    float4 finalColor = texColor * input.color;
    clip(finalColor.a - 0.01);
    return finalColor;
}