// ============================================
// ParticleVS.hlsl
// 粒子描画VS — SV_VertexIDからBillboard quad生成
// 頂点バッファなし、粒子データはSRVから読む
// ============================================

#include "Common/ParticleCommon.hlsli"

// --- 描画用定数バッファ ---
cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    float _pad0;
};

// --- 粒子データ（読み取り専用）---
StructuredBuffer<GPUParticle> particles : register(t0);

// --- VS出力 ---
struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation int atlasRows : TEXCOORD1;
    nointerpolation int atlasCols : TEXCOORD2;
    nointerpolation int uvFrame : TEXCOORD3;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output = (VSOutput) 0;

    // 粒子インデックスとquad頂点インデックスを算出
    uint particleIndex = vertexID / 6;
    uint cornerIndex = vertexID % 6;

    GPUParticle p = particles[particleIndex];

    // 死んでいる粒子は退化三角形（画面外に飛ばす）
    if (p.isAlive < 0.5)
    {
        output.position = float4(0, 0, -1, 1);
        return output;
    }

    // quadの4頂点オフセット（三角形2枚 = 6頂点）
    // 0--1
    // |\ |
    // | \|
    // 2--3
    // 三角形1: 0,1,2  三角形2: 2,1,3
    static const float2 corners[6] =
    {
        float2(-1, 1), // 0: 左上
        float2(1, 1), // 1: 右上
        float2(-1, -1), // 2: 左下
        float2(-1, -1), // 2: 左下
        float2(1, 1), // 1: 右上
        float2(1, -1), // 3: 右下
    };

    // UV座標
    static const float2 uvs[6] =
    {
        float2(0, 0), // 左上
        float2(1, 0), // 右上
        float2(0, 1), // 左下
        float2(0, 1), // 左下
        float2(1, 0), // 右上
        float2(1, 1), // 右下
    };

    float2 corner = corners[cornerIndex];
    output.uv = uvs[cornerIndex];
    
    output.atlasRows = p.atlasRows;
    output.atlasCols = p.atlasCols;
    output.uvFrame = p.uvFrame;
    // Billboard: カメラのRight/Up方向を取得
    float3 camRight = float3(g_View[0][0], g_View[1][0], g_View[2][0]);
    float3 camUp = float3(g_View[0][1], g_View[1][1], g_View[2][1]);

    // 回転適用
    float cosR = cos(p.rotation);
    float sinR = sin(p.rotation);
    float2 rotated;
    rotated.x = corner.x * cosR - corner.y * sinR;
    rotated.y = corner.x * sinR + corner.y * cosR;

    // ワールド座標でのquad頂点位置
    float halfSize = p.size * 0.5;
    float3 worldPos = p.position
                    + camRight * rotated.x * halfSize
                    + camUp * rotated.y * halfSize;

    // ビュー → プロジェクション変換
    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    output.position = mul(viewPos, g_Projection);

    // 色
    output.color = p.color;

    return output;
}