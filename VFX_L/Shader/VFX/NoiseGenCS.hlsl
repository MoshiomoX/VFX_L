// ============================================================
// NoiseGenCS.hlsl
// レシピからタイル可能な単チャンネルのノイズテクスチャを焼く。
//   type 0 perlin  : 勾配ノイズ、格子は `frequency` で折り返す
//   type 1 worley  : 一番近い特徴点までの距離、セルは折り返す
//   type 2 fbm     : perlin を `octaves` 回足す、1 回ごとに周波数 x2
// 出力は [0,1] の R32_FLOAT。1 テクセルに 1 スレッド。
// ============================================================
RWTexture2D<float> dst : register(u0);

cbuffer NoiseCB : register(b0)
{
    uint g_Type;
    uint g_Size;
    uint g_Frequency;
    uint g_Octaves;
    float g_Persistence;
    uint g_Seed;
    uint2 _pad;
};

// 整数ハッシュ -> [0,1)
float Hash(uint2 p, uint seed)
{
    uint h = p.x * 374761393u + p.y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= h >> 16u;
    return (float) (h & 0x00FFFFFFu) / 16777216.0;
}

float2 Gradient(uint2 p, uint seed)
{
    float a = Hash(p, seed) * 6.28318530718;
    return float2(cos(a), sin(a));
}

// 周期的な勾配ノイズ、周期 = per セル。[-1,1] を返す
float Perlin(float2 uv, uint per, uint seed)
{
    float2 p = uv * (float) per;
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // 5 次の補間

    uint2 i0 = (uint2) i;
    uint2 i1 = i0 + uint2(1, 0);
    uint2 i2 = i0 + uint2(0, 1);
    uint2 i3 = i0 + uint2(1, 1);
    // テクスチャがタイルするように折り返す
    i0 %= per;
    i1 %= per;
    i2 %= per;
    i3 %= per;

    float d0 = dot(Gradient(i0, seed), f - float2(0, 0));
    float d1 = dot(Gradient(i1, seed), f - float2(1, 0));
    float d2 = dot(Gradient(i2, seed), f - float2(0, 1));
    float d3 = dot(Gradient(i3, seed), f - float2(1, 1));

    float v = lerp(lerp(d0, d1, u.x), lerp(d2, d3, u.x), u.y);
    return v * 1.41421356; // ~[-1,1] に引き伸ばす
}

// 周期的な worley：周囲 9 セルの特徴点のうち一番近いものまでの距離
float Worley(float2 uv, uint per, uint seed)
{
    float2 p = uv * (float) per;
    float2 i = floor(p);
    float2 f = frac(p);

    float minD = 8.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float2 o = float2(x, y);
            uint2 cell = (uint2) ((i + o + (float) per) % (float) per);
            float2 fp = float2(Hash(cell, seed), Hash(cell, seed + 7919u));
            float2 d = o + fp - f;
            minD = min(minD, dot(d, d));
        }
    return sqrt(minD); // [0, ~1.4]
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_Size || id.y >= g_Size)
        return;

    float2 uv = (id.xy + 0.5) / (float) g_Size;
    float v;

    if (g_Type == 0u)
    {
        v = Perlin(uv, g_Frequency, g_Seed) * 0.5 + 0.5;
    }
    else if (g_Type == 1u)
    {
        v = saturate(Worley(uv, g_Frequency, g_Seed));
    }
    else
    {
        float sum = 0.0, amp = 1.0, norm = 0.0;
        uint per = g_Frequency;
        for (uint o = 0; o < g_Octaves; ++o)
        {
            sum += Perlin(uv, per, g_Seed + o * 131u) * amp;
            norm += amp;
            amp *= g_Persistence;
            per *= 2u;
        }
        v = (sum / max(norm, 1e-4)) * 0.5 + 0.5;
    }

    dst[id.xy] = saturate(v);
}