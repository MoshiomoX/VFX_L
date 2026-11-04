// ============================================================
// DistortApplyPS.hlsl
// パーティクルの画面の歪みを、UI を描く「前」にシーンへ掛ける
// （2026-10-20：以前は合成パスでやっていて、HUD まで歪んでいた）。
// 全画面の三角形（CompositeVS）でシーンの HDR ターゲットへ描く：
// 色 = uv + オフセット x 強さ の位置でサンプルした resolve 済みのシーン。
// オフセットの無いピクセルはそのまま戻る。
// ============================================================
Texture2D sceneTex : register(t0);     // resolve 済みの HDR シーン（このパスの前）
Texture2D distortTex : register(t1);   // ParticleDistortPS が書いた uv オフセット、resolve 済み
SamplerState linearClamp : register(s0);

cbuffer DistortApplyCB : register(b0)
{
    float g_Strength;   // オフセットに掛ける
    float g_Aspect;     // 幅 / 高さ（縦横で同じピクセル距離にする）
    float2 _pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput i) : SV_TARGET
{
    float2 d = distortTex.Sample(linearClamp, i.uv).xy * g_Strength;
    d.y *= g_Aspect;
    float2 uv = saturate(i.uv + d);
    return float4(sceneTex.Sample(linearClamp, uv).rgb, 1.0);
}
