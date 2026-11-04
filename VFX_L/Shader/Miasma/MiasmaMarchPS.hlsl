// ============================================================
// MiasmaMarchPS.hlsl
// 遺跡の瘴気の箱をレイマーチする（World/MiasmaField、2026-11-03）。
// 半分の解像度で描く（全画面三角形、Sky/SkyVS）: 半解像度の各ピクセルは
// 全解像度の 2p + 1 のピクセルのシーン深度を取り（合成パスも同じ規則）、
// ワールドの点を復元して、カメラからその点（または g_MaxDist）までレイを進める。
// レイは箱が覆う高さの板と、箱の xz の正方形で切り詰める。
//
// 各ステップで地面の高さと (x, 地面からの高さ, z) の密度を読み、
// 散乱で入る色と透過率を積み上げる（Beer-Lambert）。ステップは
// interleaved gradient noise で揺らす。TAA はないので、その模様は
// 合成パスのエッジを保つフィルタで均す。
// トゥーン: 不透明度を g_Bands 段に切り（縁は g_BandSoft でぼかす）、
// 色もそれに合わせて縮める（乗算済みの出力）ので、霧が他のセル
// シェーディングと同じく平らな層に見える。
// 出力: rgb = 乗算済みの色、a = 不透明度（1 - 透過率）。
// ============================================================

Texture2DMS<float> g_Depth : register(t0);
Texture3D<float> g_Vol : register(t1);
Texture2D<float> g_Height : register(t2);
Texture3D<float> g_Noise : register(t3);     // タイル可能な細部ノイズ（バリュー + 反転 Worley）、ラップする
SamplerState g_Linear : register(s0);
SamplerState g_Wrap : register(s1);

// MiasmaField::MarchCB（208 バイト）と一致させること
cbuffer MiasmaMarchCB : register(b0)
{
    row_major float4x4 g_InvViewProj;
    float3 g_CamPos;
    float g_MaxDist;
    float2 g_Origin;        // 箱の南西の角（xz）
    float2 g_MapOrigin;     // 高さマップ
    float2 g_MapSize;
    float2 g_FullSize;      // 全解像度の画面
    float g_YMin;           // 箱の中で一番低い地面
    float g_YMax;           // 一番高い地面 + 箱の天井
    float g_Extinction;     // 密度 1 のときの 1m あたりの減衰
    float g_Steps;
    float3 g_ColorLow;
    float g_Bands;
    float3 g_ColorHigh;
    float g_BandSoft;
    float g_TopBlend;       // m: 低い所の色 -> 高い所の色
    float g_Glow;           // 段の縁を明るくする量
    float g_EdgeFade;       // m: 箱の側面の近くで薄くする
    float _pad;
    float3 g_DetailOffset;  // ノイズは風に流され、上へ昇る
    float g_DetailScale;    // 1/m（大きいオクターブ）
    float g_Erode;          // 0 = 侵食なし
    float g_DensRef;        // 霧を濃い芯とみなす密度
    float g_DetailScale2;   // 1/m（小さいオクターブ）
    float g_Detail2Mix;
};

#define MIASMA_NX 128.0
#define MIASMA_NY 32.0
#define MIASMA_CELL 0.5
#define MIASMA_CELL_Y 0.3

float IGN(float2 p)
{
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

// 1 軸で lo <= c <= hi に入るレイの [t0, t1]（c = o + d t）
void ClipAxis(float o, float d, float lo, float hi, inout float t0, inout float t1)
{
    if (abs(d) < 1e-5)
    {
        if (o < lo || o > hi)
            t1 = -1.0;
        return;
    }
    float a = (lo - o) / d, b = (hi - o) / d;
    t0 = max(t0, min(a, b));
    t1 = min(t1, max(a, b));
}

float4 main(float4 pos : SV_POSITION) : SV_Target
{
    int2 full = clamp(int2(pos.xy) * 2 + 1, int2(0, 0), int2(g_FullSize) - 1);
    float d = g_Depth.Load(full, 0);
    float2 ndc = (float2(full) + 0.5) / g_FullSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 wp = mul(float4(ndc, d, 1.0), g_InvViewProj);
    float3 end = wp.xyz / wp.w;
    float3 dir = end - g_CamPos;
    float len = length(dir);
    dir /= max(len, 1e-4);

    float t0 = 0.0, t1 = min(len, g_MaxDist);
    float boxW = MIASMA_NX * MIASMA_CELL;
    ClipAxis(g_CamPos.y, dir.y, g_YMin, g_YMax, t0, t1);
    ClipAxis(g_CamPos.x, dir.x, g_Origin.x, g_Origin.x + boxW, t0, t1);
    ClipAxis(g_CamPos.z, dir.z, g_Origin.y, g_Origin.y + boxW, t0, t1);
    if (t1 <= t0)
        return float4(0.0, 0.0, 0.0, 0.0);

    float steps = max(g_Steps, 4.0);
    float dt = (t1 - t0) / steps;
    float jitter = IGN(pos.xy);
    float top = MIASMA_NY * MIASMA_CELL_Y;
    float T = 1.0;
    float3 C = 0.0;
    [loop]
    for (float i = 0.0; i < steps; i += 1.0)
    {
        float t = t0 + (i + jitter) * dt;
        float3 w = g_CamPos + dir * t;
        float ground = g_Height.SampleLevel(g_Linear, (w.xz - g_MapOrigin) / g_MapSize, 0);
        float h = w.y - ground;
        if (h < 0.0 || h > top)
            continue;
        float2 local = w.xz - g_Origin;
        float3 uvw = float3(local.x / boxW, h / top, local.y / boxW);
        float dens = g_Vol.SampleLevel(g_Linear, uvw, 0);
        // シミュレーションの滑らかな密度を細部ノイズで侵食する（ボリュメトリッククラウドの手法）:
        // 薄い縁は煙の筋に崩れ、濃い芯は残る。これがないと霧がスライムのように見えた
        if (g_Erode > 0.0 && dens > 1e-3)
        {
            float3 np = float3(w.x, h, w.z) - g_DetailOffset;
            float n1 = g_Noise.SampleLevel(g_Wrap, np * g_DetailScale, 0);
            float n2 = g_Noise.SampleLevel(g_Wrap, np * g_DetailScale2 + 0.37, 0);
            float nn = lerp(n1, n2, g_Detail2Mix);
            float x = dens / max(g_DensRef, 1e-3);
            float cut = nn * g_Erode;
            float xc = min(x, 1.0);   // 芯の密度までだけ remap する。それより濃い分はそのまま
            dens = (max(xc - cut, 0.0) / max(1.0 - cut, 0.05) + max(x - 1.0, 0.0)) * g_DensRef;
        }
        float edge = min(min(local.x, boxW - local.x), min(local.y, boxW - local.y));
        dens *= saturate(edge / max(g_EdgeFade, 0.1));
        if (dens <= 1e-3)
            continue;
        float a = 1.0 - exp(-dens * g_Extinction * dt);
        float3 col = lerp(g_ColorLow, g_ColorHigh, saturate(h / max(g_TopBlend, 0.1)));
        C += T * a * col;
        T *= 1.0 - a;
        if (T < 0.02)
            break;
    }
    float alpha = 1.0 - T;
    if (alpha < 1e-3)
        return float4(0.0, 0.0, 0.0, 0.0);

    // ---- トゥーン: 不透明度を平らな層に切る ----
    float bands = max(g_Bands, 1.0);
    float b = alpha * bands;
    float f = frac(b);
    float soft = max(g_BandSoft, 1e-3);
    float stepped = (floor(b) + smoothstep(0.5 - soft, 0.5 + soft, f)) / bands;
    // 各段の縁ちょうどにかすかな光（層の縁）
    float rim = 1.0 - saturate(abs(f - 0.5) / (soft * 2.0 + 0.02));
    float3 avg = C / alpha;
    float3 col = avg * (1.0 + g_Glow * rim * 4.0) + g_ColorHigh * (g_Glow * rim);
    return float4(col * stepped, stepped);
}
