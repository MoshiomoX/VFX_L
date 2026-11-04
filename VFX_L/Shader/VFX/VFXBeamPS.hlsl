// ============================================================
// VFXBeamPS.hlsl
// 層ごとの断面の形 x ビームに沿って流れる筋。
// 乗算済みで alpha 0 として書くので、ONE / INV_SRC_ALPHA の合成で
// 加算になる（加算のパーティクルと同じ）。HDR の色が
// ブルームにかかる。
// ============================================================
#include "../Common/BeamCommon.hlsli"

StructuredBuffer<BeamItem> g_Beams : register(t0);

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint beam : TEXCOORD1;
    nointerpolation uint layer : TEXCOORD2;
    nointerpolation float len : TEXCOORD3;
};

float4 main(PSIn i) : SV_TARGET
{
    BeamItem b = g_Beams[i.beam];
    float d = i.uv.x;
    float y = abs(i.uv.y);
    float r = saturate(1.0 - y * y); // 中心線で 1、縁で 0
    float n = BeamFbm(float2((d - b.scroll) * b.noiseScale, i.uv.y * 1.3 + b.seed));
    // トゥーンの縁に使う勾配。分岐の外で取る
    float rW = max(fwidth(r), 1e-3);
    float nW = max(fwidth(n), 1e-3);
    bool toon = b.toon > 0.5;

    // ---- 断面 ----
    // トゥーン: 滑らかに減衰させず、どの層もくっきりした縁の平らな帯にする（光輪・主色・芯が
    // 中心へ向かってはっきりした 3 段に重なる）
    float a;
    float3 col;
    if (i.layer == 0)
    {
        a = (toon ? smoothstep(0.30 - rW, 0.30 + rW, r) : r * r) * b.glowAlpha;
        col = b.color.rgb * 0.7;
    }
    else if (i.layer == 1)
    {
        a = toon ? smoothstep(0.35 - rW, 0.35 + rW, r) : pow(r, 1.6);
        col = b.color.rgb;
    }
    else
    {
        a = toon ? smoothstep(0.45 - rW, 0.45 + rW, r) : pow(r, 0.8);
        col = b.coreColor.rgb;
    }

    // ---- 長さ方向: 根元でフェードイン、先端でフェードアウト ----
    float root = saturate(d / max(b.rootFade, 0.01));
    float tip = saturate((i.len - d) / max(b.tipFade, 0.01));
    a *= root * tip;

    // ---- 流れる筋（先端へ向かう）。芯は明るさをほぼ保つ ----
    // トゥーン: 滑らかな傾斜ではなく、くっきりした境目の 2 段
    float nn = toon ? smoothstep(0.55 - nW, 0.55 + nW, n) : n;
    float streak = lerp(1.0 - b.noiseStrength, 1.0 + b.noiseStrength * 0.7, nn);
    if (i.layer == 2) streak = lerp(1.0, streak, 0.35);
    a *= streak;

    a *= (i.layer == 2) ? b.coreColor.a : b.color.a;
    clip(a - 0.002);
    return float4(col * a, 0.0);
}
