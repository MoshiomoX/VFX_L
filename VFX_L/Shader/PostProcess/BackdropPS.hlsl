// ============================================================
// BackdropPS.hlsl
// タイトル / リザルト画面の全画面の背景（2026-10-09）。
// Sky/SkyVS（全画面三角形）でシーンの HDR RT に描く。
//
// mode 0（写真）：撮っておいた戦闘のフレーム（SceneSnapshot）を拡大して
//   中央が画面中央より右に来るようにずらし、文字のパネルが載る左側を
//   暗くし、ビネットをかける。出力は不透明（alpha 1）。
//   写真がない（hasPhoto 0）ときは代わりに暗い青紫のグラデーション。
// mode 1（スクリム）：テクスチャなし。左で濃く、右（と下端）へ
//   薄れていくアルファを持つ黒を、動いているタイトルシーンの上に
//   乗算済みアルファのブレンドで描く。
// ============================================================

Texture2D g_Photo : register(t0);
SamplerState g_Linear : register(s0);

cbuffer BackdropCB : register(b0)
{
    float2 photoCenter;   // 写真の中央を置く画面上の位置（0..1）
    float  zoom;          // 画面の幅に映す写真の割合（0.8 = 80 %）
    float  brightness;
    float  shadeStrength; // 左側をどれだけ暗くするか（0..1）
    float  shadeFrom;     // 画面 x（0..1）：これより左が最も暗い
    float  shadeTo;       // 画面 x（0..1）：これより右は暗くしない
    float  vignette;
    float  desaturate;
    float  bottomShade;   // 下端に向けた追加の暗さ（操作案内用）
    float  mode;          // 0 写真、1 スクリム
    float  hasPhoto;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float4 main(PSIn i) : SV_TARGET
{
    float2 uv = i.ndc * float2(0.5, -0.5) + 0.5;   // 画面 0..1、y は下向き

    // 両モード共通の左側（と下端）の暗さ
    float side = 1.0 - smoothstep(shadeFrom, shadeTo, uv.x);
    float bottom = smoothstep(0.70, 1.0, uv.y) * bottomShade;
    float shade = saturate(side * shadeStrength + bottom);

    if (mode > 0.5)
        return float4(0.0, 0.0, 0.0, shade);   // 乗算済みアルファの黒

    float3 c;
    if (hasPhoto > 0.5)
    {
        float2 src = (uv - photoCenter) * zoom + 0.5;
        c = g_Photo.SampleLevel(g_Linear, src, 0).rgb;
        float l = dot(c, float3(0.2126, 0.7152, 0.0722));
        c = lerp(c, l.xxx, desaturate) * brightness;
    }
    else
    {
        // 写真なし：深い夜空のグラデーション（リニアの値）
        float t = saturate(uv.y);
        c = lerp(float3(0.010, 0.012, 0.030), float3(0.030, 0.020, 0.045), t);
    }

    float2 d = uv - 0.5;
    float vig = 1.0 - vignette * saturate(dot(d, d) * 2.2);
    c *= vig * (1.0 - shade);
    return float4(c, 1.0);
}
