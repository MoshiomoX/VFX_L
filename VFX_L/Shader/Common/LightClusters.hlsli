// ============================================================
// LightClusters.hlsli
// クラスタ化フォワードシェーディング（「Forward+」、2026-10-23）。
//
// カメラの視錐台を LIGHT_CLUSTER_X x LIGHT_CLUSTER_Y の
// 画面タイル x LIGHT_CLUSTER_Z の深度スライス（深度方向は指数的）に切る。
// フレームに 1 回、ライトのリストが揃った後（CPU のライト + GPU の
// 弾 / 範囲）、LightClusterCS がクラスタごとに、球がそのクラスタに触れる
// 点光源のビットマスクを書く。
// PointLightShade（Lighting.hlsli）はピクセルをそのクラスタに投影し、
// リスト全体ではなく立っているビットだけを回す。どのピクセルにも
// 届かないライトにはもうコストがかからない。
//
// クラスタ情報はマスクと並べてバインドする。このフレームで作られて
// いないとき（カメラの無いエディタのシーン、またはオフ）は SRV が
// null で、情報は 0（enabled = 0）として読め、PS は
// 以前のリスト全体のループに戻る。
//
// C++ 側: Graphics/Light/LightClusters.h（LightClusterInfo、160 バイト）
// ============================================================
#ifndef LIGHT_CLUSTERS_HLSLI
#define LIGHT_CLUSTERS_HLSLI

#include "PointLights.hlsli"   // PointLight, MAX_POINT_LIGHTS

#define LIGHT_CLUSTER_X 16
#define LIGHT_CLUSTER_Y 9
#define LIGHT_CLUSTER_Z 24
#define LIGHT_CLUSTER_COUNT (LIGHT_CLUSTER_X * LIGHT_CLUSTER_Y * LIGHT_CLUSTER_Z)
#define LIGHT_CLUSTER_WORDS (MAX_POINT_LIGHTS / 32u)   // ライト 1 つに 1 ビット

struct LightClusterInfo
{
    // view * proj の行、行ベクトルの規約: clip = x*r0 + y*r1 + z*r2 + r3
    float4 viewProj0;
    float4 viewProj1;
    float4 viewProj2;
    float4 viewProj3;
    // ビュー行列の行（右手系: カメラの前方 = -z）
    float4 view0;
    float4 view1;
    float4 view2;
    float4 view3;
    float2 projScale;   // proj._11, proj._22: 深度 d でのビュー x (y) = ndc * d / scale
    float  zNear;       // 最初のスライス境界 (m)。スライス 0 はそれでもカメラから始まる
    float  zFar;        // 最後のスライス境界 (m)。最後のスライスは無限遠まで
    float  sliceScale;  // LIGHT_CLUSTER_Z / ln(zFar / zNear)
    float  sliceBias;   // -ln(zNear) * sliceScale: slice = floor(ln(depth) * scale + bias)
    uint   enabled;     // 0 = このフレームで作られていない: リスト全体をループ
    uint   debugView;   // 1 = 各ピクセルが訪れたライト数のヒートマップ
};

// スライス境界 k（k = 0..LIGHT_CLUSTER_Z）をビューの深度で返す
// （出口は 1 つ: 途中で return すると fxc が「未初期化の可能性」と言う）
float LightClusterSliceDepth(LightClusterInfo info, uint k)
{
    float d = info.zNear * exp((float) k / info.sliceScale);
    d = (k == 0u) ? 0.0 : d;
    d = (k >= (uint) LIGHT_CLUSTER_Z) ? 1.0e5 : d;
    return d;
}

#ifndef LIGHT_CLUSTER_NO_PS_SRVS
StructuredBuffer<uint> g_LightClusterMasks : register(t10);
StructuredBuffer<LightClusterInfo> g_LightClusterInfo : register(t11);

// ワールド位置が入るクラスタ（マスクへの番号 / LIGHT_CLUSTER_WORDS）
uint LightClusterOf(LightClusterInfo info, float3 worldPos)
{
    float4 clip = worldPos.x * info.viewProj0 + worldPos.y * info.viewProj1
                + worldPos.z * info.viewProj2 + info.viewProj3;
    float depth = max(clip.w, 1e-4);   // = 透視投影ならビューの深度
    float2 ndc = clip.xy / depth;
    int tx = clamp((int) floor((ndc.x * 0.5 + 0.5) * LIGHT_CLUSTER_X), 0, LIGHT_CLUSTER_X - 1);
    int ty = clamp((int) floor((0.5 - ndc.y * 0.5) * LIGHT_CLUSTER_Y), 0, LIGHT_CLUSTER_Y - 1);
    int tz = clamp((int) floor(log(depth) * info.sliceScale + info.sliceBias), 0, LIGHT_CLUSTER_Z - 1);
    return (uint) ((tz * LIGHT_CLUSTER_Y + ty) * LIGHT_CLUSTER_X + tx);
}

// デバッグ表示: 訪れたライト数 -> 青 (1) .. 緑 (4) .. 赤 (16+)
float3 LightClusterHeat(uint lights)
{
    float t = saturate(log2((float) max(lights, 1u)) / 4.0);
    float3 c = t < 0.5 ? lerp(float3(0.1, 0.3, 1.0), float3(0.1, 1.0, 0.2), t * 2.0)
                       : lerp(float3(0.1, 1.0, 0.2), float3(1.0, 0.1, 0.05), t * 2.0 - 1.0);
    return lights == 0u ? float3(0, 0, 0) : c * 0.35;
}
#endif

#endif
