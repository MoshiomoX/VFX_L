// ============================================================
// LightingShadow.hlsli
// 太陽の影（カスケード 3 段の PCF、SunShadow）とカスケードの色分け。Lighting.hlsli の中から
// include する（LightBuffer の shadow* / cameraPosition を読む）。
// ============================================================
#ifndef LIGHTING_SHADOW_HLSLI
#define LIGHTING_SHADOW_HLSLI

// 全カスケードの深度（1 段ごとに配列の 1 スライス）と比較サンプラー
// （LESS_EQUAL、境界外 = 日なた）。ShadowMap がフレームに一度バインドする。
// それ以外の場所ではこのレジスタは使わない
Texture2DArray g_ShadowMap : register(t9);
SamplerComparisonState g_ShadowSampler : register(s2);

// ------------------------------------------------------------
// 太陽の影: 1 = 日なた、0 = 完全に影（強さを掛ける前）。
// 点を含む最初のカスケードを使う（カスケード 0 が一番くっきり）。
// 面が自分自身に影を落とさないよう、点を法線方向に数テクセル押し出し
// （太陽が面をかすめるほど多めに）、(2r+1)^2 回のバイリニア比較タップで
// PCF をかける。最後のカスケードの終わりに向けてフェードアウトする。
//
// cascadeOut = 使ったカスケード、3 = なし（デバッグの色分け用）
// ------------------------------------------------------------
float SunShadow(float3 worldPos, float3 N, float NdotL, out uint cascadeOut)
{
    cascadeOut = 3u;
    float vis = 1.0;
    if (shadowSplits.w > 0.5 && NdotL > 0.0)
    {
        float4 lp = float4(0, 0, 0, 1);
        [unroll]
        for (uint c = 0u; c < 3u; ++c)
        {
            if (cascadeOut == 3u)
            {
                float3 p = worldPos + N * (shadowTexelWorld[c] * shadowParams.y * (2.0 - NdotL));
                float4 q = mul(float4(p, 1.0), shadowViewProj[c]);
                if (all(abs(q.xy) < 0.98) && q.z > 0.0 && q.z < 1.0)
                {
                    cascadeOut = c;
                    lp = q;
                }
            }
        }
        if (cascadeOut < 3u)
        {
            float2 uv = lp.xy * float2(0.5, -0.5) + 0.5;
            float depth = lp.z - shadowParams2.y;
            float texel = shadowParams.x;
            int r = (int) shadowParams.w;
            float sum = 0.0;
            float n = 0.0;
            for (int y = -r; y <= r; ++y)
            {
                for (int x = -r; x <= r; ++x)
                {
                    sum += g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                        float3(uv + float2(x, y) * texel, (float) cascadeOut), depth);
                    n += 1.0;
                }
            }
            vis = sum / max(n, 1.0);

            float dist = distance(worldPos, cameraPosition);
            float fadeStart = shadowSplits.z * shadowParams2.x;
            vis = lerp(vis, 1.0, saturate((dist - fadeStart) / max(shadowSplits.z - fadeStart, 1e-3)));
            vis = lerp(1.0, vis, shadowParams.z);
        }
    }
    return vis;
}

// デバッグ: カスケード 0/1/2 を赤/緑/青に色分け（shadowParams2.z = 1）
float3 ShadowCascadeTint(float3 color, uint cascade)
{
    float3 tint = float3(1, 1, 1);
    if (shadowParams2.z > 0.5 && cascade < 3u)
        tint = (cascade == 0u) ? float3(1.0, 0.55, 0.55) : (cascade == 1u) ? float3(0.55, 1.0, 0.55) : float3(0.55, 0.55, 1.0);
    return color * tint;
}

#endif
