// ============================================================
// NoiseGenCS.hlsl
// Bakes a tileable single-channel noise texture from a recipe.
//   type 0 perlin  : gradient noise, lattice wraps at `frequency`
//   type 1 worley  : distance to nearest feature point, cells wrap
//   type 2 fbm     : perlin summed over `octaves`, frequency x2 each
// Output is R32_FLOAT in [0,1]. One thread per texel.
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

// integer hash -> [0,1)
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

// periodic gradient noise, period = per cells. returns [-1,1]
float Perlin(float2 uv, uint per, uint seed)
{
    float2 p = uv * (float) per;
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // quintic

    uint2 i0 = (uint2) i;
    uint2 i1 = i0 + uint2(1, 0);
    uint2 i2 = i0 + uint2(0, 1);
    uint2 i3 = i0 + uint2(1, 1);
    // wrap so the texture tiles
    i0 %= per;
    i1 %= per;
    i2 %= per;
    i3 %= per;

    float d0 = dot(Gradient(i0, seed), f - float2(0, 0));
    float d1 = dot(Gradient(i1, seed), f - float2(1, 0));
    float d2 = dot(Gradient(i2, seed), f - float2(0, 1));
    float d3 = dot(Gradient(i3, seed), f - float2(1, 1));

    float v = lerp(lerp(d0, d1, u.x), lerp(d2, d3, u.x), u.y);
    return v * 1.41421356; // stretch to ~[-1,1]
}

// periodic worley: distance to nearest of 9 neighbouring feature points
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