// ============================================================
// BeamCommon.hlsli
// VFXBeamVS / VFXBeamPS で共用：光線のアイテム（C++ の VFXBeamItem、112 バイト）、
// 層の表、光線を「流れる」ように見せるバリューノイズ。
// ============================================================
#ifndef BEAM_COMMON_HLSLI
#define BEAM_COMMON_HLSLI

#define BEAM_SEGMENTS 32
#define BEAM_LAYERS 3

// 層 0 = 輝き（一番太く淡い）、1 = 主色、2 = 白い芯
struct BeamItem
{
    float3 start;
    float width; // 主層の全幅（m）
    float3 end;
    float coreRatio; // 芯の幅 / 主層の幅
    float4 color; // 主色（HDR 可）
    float4 coreColor;
    float glowRatio; // 輝きの幅 / 主層の幅
    float glowAlpha;
    float scroll; // 光線方向のノイズの位相（m）
    float noiseScale; // 1 m あたりのノイズのセル数
    float noiseStrength; // 幅の揺れ + 明るさの筋（0..1）
    float tipFade; // 先端が細くなっていく距離（m）
    float rootFade; // 根元がフェードインする距離（m）
    float seed;
    float toon; // 1 = 硬い縁の帯 + 2 段の筋（2026-10-04）
    // 稲妻（2026-10-15）：帯がセグメントごとに横へ折れる
    float jitter; // 大きな揺れの振幅（m）。0 = まっすぐな光線
    float jitterRate; // 1 秒あたりの形の数（引き直し）
    float jitterFine; // セグメントごとの折れの振幅（jitter に対する割合）
};

// ---- バリューノイズ（ハッシュによる、テクスチャなし） ----
float BeamHash(float2 p)
{
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

float BeamNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = BeamHash(i);
    float b = BeamHash(i + float2(1, 0));
    float c = BeamHash(i + float2(0, 1));
    float d = BeamHash(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// 2 オクターブ、0..1
float BeamFbm(float2 p)
{
    return BeamNoise(p) * 0.65 + BeamNoise(p * 2.13 + 7.7) * 0.35;
}

// 稲妻の位相：今どのランダムな形を見せるか
float BeamLightningPhase(BeamItem b, float time)
{
    return floor(time * max(b.jitterRate, 1.0)) * 7.31 + b.seed * 13.7;
}

// t（光線に沿って 0..1）にあるセグメント境界 `boundary`（0..BEAM_SEGMENTS）の
// 横方向のずれ。境界の間は折れ線なので、稲妻にくっきりした折れ目ができる。
// 両端は固定。境界を共有する 2 枚の四角形は同じずれになる
// （境界の番号が同じ）ので、隙間はできない
float3 BeamLightningOffset(BeamItem b, float boundary, float t, float3 axis, float phase)
{
    float3 up = (abs(axis.y) < 0.99) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 n1 = normalize(cross(axis, up));
    float3 n2 = cross(axis, n1);
    // 境界ごとの細かな折れ
    float2 f = float2(BeamHash(float2(boundary, phase)), BeamHash(float2(boundary + 57.0, phase))) * 2.0 - 1.0;
    // 粗い揺れ：境界 4 つごとに 1 つの値を補間する
    float cb = boundary * 0.25;
    float ci = floor(cb);
    float cf = frac(cb);
    float2 c0 = float2(BeamHash(float2(ci * 3.0 + 101.0, phase)), BeamHash(float2(ci * 3.0 + 151.0, phase))) * 2.0 - 1.0;
    float2 c1 = float2(BeamHash(float2(ci * 3.0 + 104.0, phase)), BeamHash(float2(ci * 3.0 + 154.0, phase))) * 2.0 - 1.0;
    float2 c = lerp(c0, c1, cf);
    float2 off = (f * b.jitterFine + c) * b.jitter;
    float env = saturate(t * 10.0) * saturate((1.0 - t) * 10.0);
    return (n1 * off.x + n2 * off.y) * env;
}

// 長さ len の光線上、距離 d（m）の位置での層の半幅
float BeamHalfWidth(BeamItem b, uint layer, float d, float len)
{
    float ratio = (layer == 0) ? b.glowRatio : (layer == 2) ? b.coreRatio : 1.0;
    float w = b.width * 0.5 * ratio;

    // 揺れは先端へ流れる。輝きは芯よりなめらか
    float n = BeamFbm(float2((d - b.scroll) * b.noiseScale, b.seed + layer * 3.1));
    float wob = 1.0 + b.noiseStrength * (n * 2.0 - 1.0) * ((layer == 0) ? 0.3 : 0.6);
    w *= wob;

    // 先端へ向けて細くし、根元から広げる
    float tip = saturate((len - d) / max(b.tipFade, 0.01));
    float root = saturate(d / max(b.rootFade, 0.01));
    w *= lerp(0.35, 1.0, tip) * lerp(0.6, 1.0, root);
    return w;
}

#endif
