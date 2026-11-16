// ============================================================
// GPUParticlePS.hlsl
// ビルボードのパーティクル。全パーティクルを 1 回の描画で、
// 乗算済みアルファ（ONE / INV_SRC_ALPHA）で合成する:
//   加算のパーティクルは alpha 0 を書く     -> dst + rgb*a（SRC_ALPHA/ONE と同じ）
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
    float4 g_FogColor;  // rgb = 霧の色（線形）、a = 霧の上限（0 = 霧なし）
    float4 g_FogRange;  // x = かかり始め（m）、y = 上限に達する距離（m）、z = ソフトの幅（m、0 = 無し）
    float4 g_DepthAB;   // xy = 深度 → 目からの距離（eye = B / (d - A)）
    float3 g_CamPos;
    float _pad2;
    float4 g_NoiseParams;   // トゥーンのノイズ侵食：x = 強さ、y = 模様の細かさ、z = 寿命中に流れる量、w = うねり
    float4 g_SunView;       // xyz = 太陽へ向かう向き（ビュー空間）、w = 立体感（0 = 縁から芯への段だけ、1 = 太陽の明暗で段を分ける）
    float4 g_ErodeEdge;     // ノイズ侵食の縁の色：rgb = 色（線形 HDR）、w = 幅（形の値）
    float4 g_FireRim;       // 火の色の段の一番外（濃い赤、線形 HDR）、w = 芯を白へ寄せる量
};

// シーンの深度（MSAA）。ソフトパーティクルの時だけ GPUParticleSystem が張る
Texture2DMS<float> g_SceneDepth : register(t8);
// トゥーンの侵食ノイズ（グレースケール、ラップで読む）
Texture2D g_ErodeNoise : register(t9);
SamplerState g_Wrap : register(s2);

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
    float3 worldPos : TEXCOORD9;
};

// ソフトパーティクル：後ろの面までの距離（m）を幅で割った 0..1。地面や壁に刺さった所で 0
float SoftFade(float4 svPos, uint solidPass)
{
    float k = 1.0;
    if (g_FogRange.z > 0.0)
    {
        float scene = g_SceneDepth.Load(int2(svPos.xy), 0);
        float sceneEye = g_DepthAB.y / (scene - g_DepthAB.x);
        float selfEye = g_DepthAB.y / (svPos.z - g_DepthAB.x);
        k = saturate((sceneEye - selfEye) / g_FogRange.z);
    }
    return k;
}

// 距離の霧（Lighting.hlsli の ApplyFog と同じ式）。乗算済みの出力に掛ける：
// 半透明 = 霧の色へ寄せる、加算 = 遠いほど足す量を減らす
float4 ParticleFog(float4 o, float3 worldPos, bool alphaBlend)
{
    if (g_FogColor.a > 0.0)
    {
        float d = distance(worldPos, g_CamPos);
        float f = smoothstep(g_FogRange.x, max(g_FogRange.y, g_FogRange.x + 1e-3), d) * g_FogColor.a;
        o.rgb = o.rgb * (1.0 - f) + (alphaBlend ? g_FogColor.rgb * o.a * f : 0.0);
    }
    return o;
}

// ------------------------------------------------------------
// トゥーンのビルボード。renderMode のビット、
// ParticleRenderMode::PackToon（GPUParticle.h）と一致させること:
//   ビット 14 = トゥーン、ビット 15 = アウトライン、ビット 16-19 = アルファの切り（0.05 + 0.05 n）、
//   ビット 20-21 = 色の段数 - 1、ビット 22-25 = 一番外の段の明るさ（n / 15）
// ------------------------------------------------------------
#define PARTICLE_TOON         (1u << 14)
#define PARTICLE_TOON_OUTLINE (1u << 15)
#define PARTICLE_TOON_NOISE   (1u << 8)    // ノイズ侵食（ビルボードでは空きの bit。ParticleRenderMode::kToonNoise）
#define PARTICLE_TOON_FIRE    (1u << 10)   // 火の色の段：外 = 濃い赤、中 = 粒子の色、芯 = 白寄り（明るさではなく色で段を分ける。ParticleRenderMode::kToonFire）
static const float TOON_OUTLINE_WIDTH = 0.07;   // 切りの内側のアルファの幅
static const float TOON_OUTLINE_DARK = 0.22;    // 線 = 色 x これ
static const float TOON_SHAPE_BLUR = 8.0;       // 形を取るサンプルの勾配の倍率（mip 3 段ほど上）

// texRgb: 読んだテクセル、ストレートな色（もうアルファで割ってある）
// ta    : テクスチャのアルファ（形）、pc: パーティクルの色
// fade  : x = 開始時のアルファに対する残り寿命、y = 不透明度（VS から）
// aa    : fwidth(ta)、呼び出し側で分岐の外で取ったもの
// softK : ソフトパーティクルの 0..1（地面に刺さった所で 0）。トゥーンは薄くせず形を削る
// noise : 侵食ノイズ 0..1（負 = 使わない）。寿命が進むほど低い所から穴が開き、筋になって消える
// ball  : xy = 四角形の中心から外へ向かう画面上の向き（y 下向き）、z = 中心からの距離 0..1（球の法線を作る）
float4 ToonBillboard(float3 texRgb, float ta, float aa, float4 pc, float2 fade, uint mode, bool alphaBlend, float softK, float noise, float3 ball)
{
    float cut = 0.05 + 0.05 * (float)((mode >> 16u) & 15u);
    uint bands = 1u + min((mode >> 20u) & 3u, 2u);
    float shade = (float)((mode >> 22u) & 15u) / 15.0;

    float edgeOn = 0.0;   // 侵食の縁を光らせる重み（寿命の後半ほど強い）
    float ta0 = ta;       // ノイズで削る前の形（穴の縁と外周を見分ける）
    if (noise >= 0.0)
    {
        float age = 1.0 - fade.x;
        float k = g_NoiseParams.x * (0.25 + age);
        edgeOn = saturate(age * 3.0 - 0.3);
        ta = ta - k * (1.0 - noise);
        aa += k * 0.03;   // ノイズの細かさの分だけ縁をなだらかに
    }

    // 寿命によるフェードは薄くするのではなく形を侵食する（切りが上がる）
    float thr = lerp(1.0, cut, fade.x);
    thr = lerp(1.0, thr, softK);
    aa = max(aa, 1e-3);
    float mask = smoothstep(thr - aa, thr + aa, ta);
    if (mask <= 0.0)
        discard;

    // 縁から内へ向かう段: 一番外 = shade、一番内 = 1
    float inner = saturate((ta - thr) / max(1.0 - thr, 1e-3));   // 縁で 0 .. 芯で 1

    // 立体感：煙玉を球とみなした法線で太陽の明暗を取り、段はその明るさで切る（上の明るい面・下の影）。
    // 球の半径方向は四角形の中心からの距離と形の縁までの近さの平均（形のこぶにも陰が付く）
    if (g_SunView.w > 0.0)
    {
        float rr = saturate(lerp(ball.z, 1.0 - inner, 0.5));
        float3 n = float3(ball.xy * float2(1.0, -1.0) * rr, -sqrt(saturate(1.0 - rr * rr)));
        float lit = saturate(0.45 + 0.8 * dot(n, g_SunView.xyz));
        // 火の色の段は弱め（外周の赤が一周残るように）
        float w = (mode & PARTICLE_TOON_FIRE) ? g_SunView.w * 0.45 : g_SunView.w;
        inner = lerp(inner, lit * saturate(inner * 4.0 + 0.25), w);
    }
    // 火の色の段：段の境を内側へ寄せる（外の赤を太く、芯の白を小さく）
    if (mode & PARTICLE_TOON_FIRE)
        inner = pow(inner, 1.8);
    float level = 1.0;
    float t = 1.0;   // 段の位置：外 0 .. 芯 1
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
        t = b / steps;
        level = lerp(shade, 1.0, t);
    }
    float3 rgb = texRgb * pc.rgb * level;
    if (mode & PARTICLE_TOON_FIRE)
    {
        // 外 = 濃い赤（粒子が暗くなるほど赤も暗く）、中 = 粒子の色、芯 = 白寄り
        float3 rim = g_FireRim.rgb * saturate(pc.r / 1.3);
        float peak = max(pc.r, max(pc.g, pc.b));
        float3 hot = lerp(pc.rgb, peak * float3(1.0, 0.92, 0.45), g_FireRim.w);
        float3 c = lerp(lerp(rim, pc.rgb, saturate(t * 2.0)), hot, saturate(t * 2.0 - 1.0));
        rgb = texRgb * c;
    }

    // 縁のすぐ内側に暗い線
    if (mode & PARTICLE_TOON_OUTLINE)
    {
        float lineW = TOON_OUTLINE_WIDTH * (1.0 - thr) / max(1.0 - cut, 1e-3);   // 侵食とともに細くなる
        float rim = 1.0 - smoothstep(thr + lineW - aa, thr + lineW + aa, ta);   // （'line' は予約語）
        rgb = lerp(rgb, texRgb * pc.rgb * TOON_OUTLINE_DARK, rim);
    }

    // ノイズ侵食の縁：ノイズが開けた穴の境目に少し太い光る帯（燃えて崩れる縁）。火の色の段の粒子だけ（煙は光らせない）。
    // 元の形の外周（削る前でも境に近い所）は光らせない
    if (edgeOn > 0.0 && (mode & PARTICLE_TOON_FIRE) != 0u)
    {
        float ew = g_ErodeEdge.w;
        float band = 1.0 - smoothstep(thr + ew - aa, thr + ew + aa, ta);
        float hole = saturate((ta0 - thr) / max(2.0 * ew, 1e-3));
        rgb = lerp(rgb, g_ErodeEdge.rgb, band * hole * edgeOn);
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
    // 勾配を 1.8 倍 = 一つ上の mip で少しぼかした模様（細かい傷のようにならず、滑らかな筋になる）
    float2 nDx = ddx(input.uv) * g_NoiseParams.y * 1.8;
    float2 nDy = ddy(input.uv) * g_NoiseParams.y * 1.8;
    // 球の法線用：四角形の中心からの距離² の画面上の勾配 = 外向き（回転していても画面の向きで取れる）
    float2 bp = input.uv * 2.0 - 1.0;
    float br2 = dot(bp, bp);
    float2 bg = float2(ddx(br2), ddy(br2));
    float3 ball = float3(bg * rsqrt(max(dot(bg, bg), 1e-12)), saturate(sqrt(br2)));
    if (input.renderMode & PARTICLE_TOON)
    {
        // 侵食ノイズ：1 回目で読んだ値で UV をうねらせ（流体の渦）、寿命とともに縦へ流す → 筋状にほどける
        float noise = -1.0;
        if ((input.renderMode & PARTICLE_TOON_NOISE) != 0u && g_NoiseParams.x > 0.0)
        {
            float age = 1.0 - input.toonFade.x;
            float2 seed = frac(float2(0.37, 0.61) * (float)input.uvFrame);
            float2 nuv = input.uv * g_NoiseParams.y * float2(1.0, 0.6) + seed;
            float w = g_ErodeNoise.SampleGrad(g_Wrap, nuv * 0.45 + 0.5, nDx * 0.45, nDy * 0.45).r;
            float ang = w * 6.2832 + age * 2.0;
            nuv += float2(cos(ang), sin(ang)) * g_NoiseParams.w + float2(0.0, -age * g_NoiseParams.z);
            noise = g_ErodeNoise.SampleGrad(g_Wrap, nuv, nDx, nDy).r;
        }
        float3 straight = ((g_PremulMask & bit) != 0u) ? soft.rgb / max(soft.a, 1e-3) : soft.rgb;
        // 深度を書くパス: 煙玉の内側は完全に不透明（不透明度 0.85..1 -> 1。透けて見えるものが
        // 描画順に左右されなくなる）にし、深度を書くのは形の内側だけ
        // （0.5 未満の柔らかい縁まで書くと、後ろの煙玉を隠してしまう）
        float2 fade = input.toonFade;
        if (input.solidPass != 0u)
            fade.y = 1.0;
        float softK = SoftFade(input.position, input.solidPass);
        float4 toon = ToonBillboard(straight, soft.a, alphaWidth, input.color, fade, input.renderMode, input.alphaBlend != 0u, softK, noise, ball);
        if (input.solidPass != 0u)
            clip(toon.a - 0.5);
        return ParticleFog(toon, input.worldPos, input.alphaBlend != 0u);
    }

    float a = tex.a * input.color.a;
    clip(a - 0.01);

    // 乗算済みのテクセルは rgb にもうアルファが掛かっている
    float3 rgb = ((g_PremulMask & bit) != 0u)
        ? tex.rgb * input.color.rgb * input.color.a
        : tex.rgb * input.color.rgb * a;

    float4 o = float4(rgb, (input.alphaBlend != 0u) ? a : 0.0);
    o *= SoftFade(input.position, input.solidPass);
    return ParticleFog(o, input.worldPos, input.alphaBlend != 0u);
}
