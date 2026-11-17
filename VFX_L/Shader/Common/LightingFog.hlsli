// ============================================================
// LightingFog.hlsli
// 距離フォグ（ApplyFog）と雲の影（CloudSun）。Lighting.hlsli の中から include する
// （LightBuffer の fog* / cloud* / cameraPosition を読む）。
// ============================================================
#ifndef LIGHTING_FOG_HLSLI
#define LIGHTING_FOG_HLSLI

// ------------------------------------------------------------
// 距離フォグ: fogStart から fogEnd の間でライティング後の色 -> fogColor
// （smoothstep、上限 fogMax）。遠くの地形が空に溶け込む。
// 下のワールド座標付きシェーディング関数の最後でかける
// ------------------------------------------------------------
float3 ApplyFog(float3 color, float3 worldPos)
{
    float d = distance(worldPos, cameraPosition);
    float f = smoothstep(fogStart, max(fogEnd, fogStart + 1e-3), d) * fogMax;
    return lerp(color, fogColor, f);
}

// ------------------------------------------------------------
// 雲の影: 地面を流れる太陽の陰り。1 = 晴れ、1 - 強さ = 雲の下。
// 2 段の値ノイズを風で流し、覆う割合で切る（縁は少しだけぼかす）
// ------------------------------------------------------------
float CloudHash(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
float CloudNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = CloudHash(i);
    float b = CloudHash(i + float2(1, 0));
    float c = CloudHash(i + float2(0, 1));
    float d = CloudHash(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float CloudSun(float3 worldPos)
{
    float sun = 1.0;
    if (cloudParams.x > 0.0)
    {
        float2 p = (worldPos.xz - cloudOffset.xy) * cloudParams.y;
        float n = CloudNoise(p) * 0.65 + CloudNoise(p * 2.37 + 17.3) * 0.35;
        float cover = smoothstep(1.0 - cloudParams.z - cloudParams.w, 1.0 - cloudParams.z + cloudParams.w, n);
        sun = 1.0 - cloudParams.x * cover;
    }
    return sun;
}

#endif
