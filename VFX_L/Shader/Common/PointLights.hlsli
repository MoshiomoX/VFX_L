// ============================================================
// PointLights.hlsli
// フレームごとの点光源リスト。全モデルの PS が共有する。
//
// リストはフレーム全体で 1 つのバッファ:
//   [0, cpuCount)        PointLightManager が書く（CPU のエフェクトのエントリ、
//                        エディタのプレビュー、範囲の詠唱、エリート）
//   [cpuCount, count)    SwarmLightCollectCS が、レシピにライトを持つ
//                        生きている GPU の弾 / 範囲から追加する
// 数は要素 1 個のバッファに入れるので、PS は生きているライトだけを
// 回る。バインドしていない SRV は 0 が読めるので、ライトのない
// シーンはバッファ 1 回の読み込みで済む。
// 2026-10-23 からは PS がリスト全体を回らない:
// LightClusterCS がライトを画面のクラスタに振り分ける（LightClusters.hlsli）ので、
// 上限は 64 -> 256 にした（32 の倍数を保つこと: ライト 1 つにマスク 1 ビット）。
//
// C++ 側: Graphics/Light/PointLightManager.h（PointLight、32 バイト）
// ============================================================
#ifndef POINT_LIGHTS_HLSLI
#define POINT_LIGHTS_HLSLI

#define MAX_POINT_LIGHTS 256u

struct PointLight
{
    float3 position;
    float radius;      // m。これより先は光が 0
    float3 color;
    float intensity;   // 線形 HDR のゲイン（>1 でブルームがかかる）
};

#ifndef POINT_LIGHT_SRV_REG
#define POINT_LIGHT_SRV_REG t6
#endif
#ifndef POINT_LIGHT_COUNT_SRV_REG
#define POINT_LIGHT_COUNT_SRV_REG t7
#endif

StructuredBuffer<PointLight> g_PointLights : register(POINT_LIGHT_SRV_REG);
StructuredBuffer<uint> g_PointLightCount : register(POINT_LIGHT_COUNT_SRV_REG);

// 窓関数をかけた滑らかな逆二乗の減衰（半径でちょうど 0 になる）
float PointLightAttenuation(float dist, float radius)
{
    float x = saturate(1.0 - (dist * dist) / max(radius * radius, 1e-4));
    return x * x;
}

// シェーディングのループ（ライトごとの拡散 + GGX 鏡面反射）は
// Lighting.hlsli の PointLightShade にある: GGX の項と、そこで定義する
// ライトの cbuffer のカメラ位置が要るため。

#endif
