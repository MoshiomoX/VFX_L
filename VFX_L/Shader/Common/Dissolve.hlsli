// ============================================================
// Dissolve.hlsli
// ライティングするモデルの PS（PS.hlsl / PBR_PS.hlsl）用のディゾルブ。
// VFXMeshPS と同じ式：noise(uv) < threshold の所をクリップし、
// しきい値のすぐ上の帯を edgeColor で光らせる。
//   PS b1 = DissolveCB（C++ の Renderer::DissolveCB）、t5 = ノイズ。
//   threshold < 0 で無効（エンティティに DissolveComponent が
//   ないとき Renderer が -1 を書く）。
// ModelCommon.hlsli の後に include する（samplerState s0 を使う）。
// ============================================================
#ifndef DISSOLVE_HLSLI
#define DISSOLVE_HLSLI

#ifndef DISSOLVE_CB_REG
#define DISSOLVE_CB_REG b1
#endif

cbuffer DissolveCB : register(DISSOLVE_CB_REG)
{
    float2 g_DissolveTiling;
    float2 g_DissolveScroll;
    float g_DissolveThreshold; // < 0：無効
    float g_DissolveEdge;
    // 被弾フラッシュ（2026-10-09、被弾したプレイヤー）：0..1。
    // ライティング後の色を g_DissolveEdgeColor.rgb へ寄せる（ディゾルブ中も有効：プレイヤーの死亡時、金色の体 + 金色の縁）
    float g_HitFlash;
    float g_DissolvePad;
    float4 g_DissolveEdgeColor; // rgb = 光の色、a = 強さ（HDR）。g_HitFlash > 0 のときはフラッシュの色
};

Texture2D dissolveNoise : register(t5);

// 溶けたピクセルをクリップする。ライティング後の色に足す発光を返す。
float3 ApplyDissolve(float2 uv)
{
    // 出口を 1 つにする：2 つの return の間に clip() があると fxc が X4000 の警告を出す
    float3 glow = float3(0, 0, 0);
    if (g_DissolveThreshold >= 0.0)
    {
        float n = dissolveNoise.Sample(samplerState, uv * g_DissolveTiling + g_DissolveScroll).r;
        float d = n - g_DissolveThreshold;
        clip(d);
        float edge = 1.0 - saturate(d / max(g_DissolveEdge, 1e-4));
        glow = g_DissolveEdgeColor.rgb * (edge * g_DissolveEdgeColor.a);
    }
    return glow;
}

// 被弾フラッシュ：最終的な色をフラッシュの色へ lerp する（g_HitFlash が 0 なら何もしない）
float3 ApplyHitFlash(float3 c)
{
    if (g_HitFlash > 0.0)
        c = lerp(c, g_DissolveEdgeColor.rgb, saturate(g_HitFlash));
    return c;
}

#endif
