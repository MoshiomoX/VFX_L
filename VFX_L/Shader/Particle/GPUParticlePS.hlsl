// ============================================================
// GPUParticlePS.hlsl
// ビルボードのパーティクル。全パーティクルを 1 回の描画で、
// 乗算済みアルファ（ONE / INV_SRC_ALPHA）で合成する:
//   加算のパーティクルは alpha 0 を書く     -> dst + rgb*a（以前の SRC_ALPHA/ONE と同じ）
//   半透明のパーティクルは alpha a を書く   -> dst*(1-a) + rgb*a
// テクスチャはパーティクルごとに ParticleSheets の表（t0..t7）から選ぶ。
// SM5.0 ではテクスチャ配列を動的な番号で引けないので switch にしている。
// 分岐はパーティクル内で一様（四角形が 2 つのプリミティブにまたがる
// ことはない）で、勾配は分岐の前に取る。
// ============================================================

Texture2D g_Sheet0 : register(t0);
Texture2D g_Sheet1 : register(t1);
Texture2D g_Sheet2 : register(t2);
Texture2D g_Sheet3 : register(t3);
Texture2D g_Sheet4 : register(t4);
Texture2D g_Sheet5 : register(t5);
Texture2D g_Sheet6 : register(t6);
Texture2D g_Sheet7 : register(t7);

SamplerState g_Linear : register(s0);
SamplerState g_Point : register(s1);

// ビット i = シート i。GPUParticleSystem::Render がマニフェストから書く
cbuffer ParticleSheetCB : register(b0)
{
    uint g_PointMask;   // ポイントサンプラーで読む（ピクセルアート）
    uint g_PremulMask;  // テクセルが乗算済みで保存されている
    uint2 _pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    // uint: GPU で符号付き整数の除算 / 剰余をすると、符号の処理に
    // エミュレーション命令が要る（fxc の警告 X3556）。
    // ここの値は負にならないので、uint で安全かつ速い。
    nointerpolation uint atlasRows : TEXCOORD1;
    nointerpolation uint atlasCols : TEXCOORD2;
    nointerpolation uint uvFrame : TEXCOORD3;
    nointerpolation uint sheet : TEXCOORD4;
    nointerpolation uint alphaBlend : TEXCOORD5;
    nointerpolation uint renderMode : TEXCOORD6;
    nointerpolation float2 toonFade : TEXCOORD7;   // x = 残り寿命（侵食）、y = 不透明度
    nointerpolation uint solidPass : TEXCOORD8;    // 1 = 不透明なトゥーンの煙玉を深度に書くパス（GPUParticleVS g_Pass）
};

// ------------------------------------------------------------
// トゥーンのビルボード（2026-10-04）。renderMode のビット、
// ParticleRenderMode::PackToon（GPUParticle.h）と一致させること:
//   ビット 14 = トゥーン、ビット 15 = アウトライン、ビット 16-19 = アルファの切り（0.05 + 0.05 n）、
//   ビット 20-21 = 色の段数 - 1、ビット 22-25 = 一番外の段の明るさ（n / 15）
// ------------------------------------------------------------
#define PARTICLE_TOON         (1u << 14)
#define PARTICLE_TOON_OUTLINE (1u << 15)
static const float TOON_OUTLINE_WIDTH = 0.07;   // 切りの内側のアルファの幅
static const float TOON_OUTLINE_DARK = 0.22;    // 線 = 色 x これ
static const float TOON_SHAPE_BLUR = 8.0;       // 形を取るサンプルの勾配の倍率（mip 3 段ほど上）

// texRgb: 読んだテクセル、ストレートな色（もうアルファで割ってある）
// ta    : テクスチャのアルファ（形）、pc: パーティクルの色
// fade  : x = 開始時のアルファに対する残り寿命、y = 不透明度（VS から）
// aa    : fwidth(ta)、呼び出し側で分岐の外で取ったもの
float4 ToonBillboard(float3 texRgb, float ta, float aa, float4 pc, float2 fade, uint mode, bool alphaBlend)
{
    float cut = 0.05 + 0.05 * (float)((mode >> 16u) & 15u);
    uint bands = 1u + min((mode >> 20u) & 3u, 2u);
    float shade = (float)((mode >> 22u) & 15u) / 15.0;

    // 寿命によるフェードは薄くするのではなく形を侵食する（切りが上がる）
    float thr = lerp(1.0, cut, fade.x);
    aa = max(aa, 1e-3);
    float mask = smoothstep(thr - aa, thr + aa, ta);
    if (mask <= 0.0)
        discard;

    // 縁から内へ向かう段: 一番外 = shade、一番内 = 1
    float inner = saturate((ta - thr) / max(1.0 - thr, 1e-3));   // 縁で 0 .. 芯で 1
    float level = 1.0;
    if (bands > 1u)
    {
        float steps = (float)(bands - 1u);
        float b = 0.0;
        [unroll]
        for (uint k = 1u; k < 3u; ++k)
        {
            if (k < bands)
            {
                float edgeK = (float)k / (float)bands;
                b += smoothstep(edgeK - aa * 2.0, edgeK + aa * 2.0, inner);
            }
        }
        level = lerp(shade, 1.0, b / steps);
    }
    float3 rgb = texRgb * pc.rgb * level;

    // 縁のすぐ内側に暗い線
    if (mode & PARTICLE_TOON_OUTLINE)
    {
        float lineW = TOON_OUTLINE_WIDTH * (1.0 - thr) / max(1.0 - cut, 1e-3);   // 侵食とともに細くなる
        float rim = 1.0 - smoothstep(thr + lineW - aa, thr + lineW + aa, ta);   // （'line' は予約語）
        rgb = lerp(rgb, texRgb * pc.rgb * TOON_OUTLINE_DARK, rim);
    }

    // 乗算済みの出力: 半透明 = 開始時の不透明度でくっきりした形、加算 = alpha 0
    float a = mask * (alphaBlend ? fade.y : 1.0);
    return float4(rgb * a, alphaBlend ? a : 0.0);
}

float4 SampleSheet(uint sheet, SamplerState s, float2 uv, float2 dx, float2 dy)
{
    switch (sheet)
    {
        case 0: return g_Sheet0.SampleGrad(s, uv, dx, dy);
        case 1: return g_Sheet1.SampleGrad(s, uv, dx, dy);
        case 2: return g_Sheet2.SampleGrad(s, uv, dx, dy);
        case 3: return g_Sheet3.SampleGrad(s, uv, dx, dy);
        case 4: return g_Sheet4.SampleGrad(s, uv, dx, dy);
        case 5: return g_Sheet5.SampleGrad(s, uv, dx, dy);
        case 6: return g_Sheet6.SampleGrad(s, uv, dx, dy);
        default: return g_Sheet7.SampleGrad(s, uv, dx, dy);
    }
}

float4 main(PSInput input) : SV_TARGET
{
    // アトラスの UV。
    // 0 の対策: アトラスを使わないエミッタは atlasRows/Cols が
    // 0 のままで、1.0/0 や "% 0" は未定義になる。
    // 0 は 1x1 の 1 マスとして扱う。
    uint cols = max(input.atlasCols, 1u);
    uint rows = max(input.atlasRows, 1u);

    float cellW = 1.0 / (float) cols;
    float cellH = 1.0 / (float) rows;

    uint col = input.uvFrame % cols;
    uint row = input.uvFrame / cols;

    float2 atlasUV = float2(
        input.uv.x * cellW + (float) col * cellW,
        input.uv.y * cellH + (float) row * cellH
    );

    float2 dx = ddx(atlasUV);
    float2 dy = ddy(atlasUV);

    uint sheet = min(input.sheet, 7u);
    uint bit = 1u << sheet;
    // ?: ではなく if/else（HLSL は ?: の両辺を評価するので 2 回サンプルしてしまう）
    float4 tex;
    [branch]
    if ((g_PointMask & bit) != 0u)
        tex = SampleSheet(sheet, g_Point, atlasUV, dx, dy);
    else
        tex = SampleSheet(sheet, g_Linear, atlasUV, dx, dy);

    // トゥーン: くっきりした形、色の段、暗い縁（エミッタごと、PARTICLE_TOON）。
    // 形はぼかしたテクセル（勾配を拡大して上の mip）から取る: そうしないと
    // ノイズの多い煙のテクスチャがしきい値でレース状になる。アルファの
    // 勾配は分岐の後、フロー制御の外で取る
    float4 soft = tex;
    [branch]
    if (input.renderMode & PARTICLE_TOON)
        soft = SampleSheet(sheet, g_Linear, atlasUV, dx * TOON_SHAPE_BLUR, dy * TOON_SHAPE_BLUR);
    float alphaWidth = fwidth(soft.a);
    if (input.renderMode & PARTICLE_TOON)
    {
        float3 straight = ((g_PremulMask & bit) != 0u) ? soft.rgb / max(soft.a, 1e-3) : soft.rgb;
        // 深度を書くパス: 煙玉の内側は完全に不透明（不透明度 0.85..1 -> 1。透けて見えるものが
        // 描画順に左右されなくなる）にし、深度を書くのは形の内側だけ
        // （0.5 未満の柔らかい縁まで書くと、後ろの煙玉を隠してしまう）
        float2 fade = input.toonFade;
        if (input.solidPass != 0u)
            fade.y = 1.0;
        float4 toon = ToonBillboard(straight, soft.a, alphaWidth, input.color, fade, input.renderMode, input.alphaBlend != 0u);
        if (input.solidPass != 0u)
            clip(toon.a - 0.5);
        return toon;
    }

    float a = tex.a * input.color.a;
    clip(a - 0.01);

    // 乗算済みのテクセルは rgb にもうアルファが掛かっている
    float3 rgb = ((g_PremulMask & bit) != 0u)
        ? tex.rgb * input.color.rgb * input.color.a
        : tex.rgb * input.color.rgb * a;

    return float4(rgb, (input.alphaBlend != 0u) ? a : 0.0);
}
