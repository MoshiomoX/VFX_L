// ============================================================
// TextOutlinePS.hlsl
// DirectXTK の SpriteBatch 用の文字のアウトライン（カスタムピクセルシェーダー、2026-10-23、
// ダメージの数字）。アウトラインはここで各ピクセルの周りを見て作るので、
// アウトライン付きの文字列は DrawString 9 回（ずらした 8 枚 + 本体）ではなく
// 2 回（アウトライン + 本体）で済む。
//   color.a <  0 : アウトラインの層。カバー率 = g_OutlinePx 画面ピクセル以内の
//                  グリフのアルファの最大値。出力 = rgb x カバー率、
//                  alpha = -color.a x カバー率（rgb は乗算済み）
//   color.a >= 0 : 本体。SpriteBatch の既定と同じ（テクスチャ x 色）
// フォントは各グリフの周りに空きの余白を付けて焼くこと
// （Tools/BuildSpriteFont.ps1 -Pad）。そうしないとアウトラインがグリフの四角形で
// 切れるか、アトラスの隣のグリフを拾う。g_MaxTexels = 余白 - 1。
// ============================================================
Texture2D<float4> g_Tex : register(t0);
SamplerState g_Samp : register(s0);

cbuffer TextOutlineCB : register(b0)
{
    float  g_OutlinePx;   // アウトラインの半径（画面ピクセル）
    float  g_MaxTexels;   // これより遠くは見ない（テクセル）
    float2 g_Pad;
};

float4 main(float4 color : COLOR0, float2 uv : TEXCOORD0) : SV_Target0
{
    float4 tex = g_Tex.Sample(g_Samp, uv);
    // 微分は分岐の外で取る（分岐の中では未定義）。下のタップは SampleGrad を使う
    float2 duvx = ddx(uv);
    float2 duvy = ddy(uv);

    // 本体: 16 回のアウトラインのタップより先に済ませる（2026-10-25: 以前は本体のピクセルもタップを読んでいた）
    [branch]
    if (color.a >= 0.0)
        return tex * color;

    // 画面ピクセル -> uv、アウトラインの半径に合わせて拡大し、余白の分でクランプ
    uint w, h;
    g_Tex.GetDimensions(w, h);
    float2 size = float2(w, h);
    float2 ux = duvx * g_OutlinePx;
    float2 uy = duvy * g_OutlinePx;
    float texels = max(length(ux * size), length(uy * size));
    float k = min(1.0, g_MaxTexels / max(texels, 1e-4));
    ux *= k;
    uy *= k;

    // 8 方向を半径いっぱいとその半分で（細い角のため）
    float a = tex.a;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float ang = i * 0.78539816;
        float2 d = cos(ang) * ux + sin(ang) * uy;
        a = max(a, g_Tex.SampleGrad(g_Samp, uv + d, duvx, duvy).a);
        a = max(a, g_Tex.SampleGrad(g_Samp, uv + d * 0.5, duvx, duvy).a);
    }

    return float4(color.rgb * a, -color.a * a);
}
