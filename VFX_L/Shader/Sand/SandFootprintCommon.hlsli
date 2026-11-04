// ============================================================
// SandFootprintCommon.hlsli
// SandFootprintVS / PS で共用（World/SandFootprints、2026-11-03）。
// ============================================================
#ifndef SAND_FOOTPRINT_COMMON
#define SAND_FOOTPRINT_COMMON

// SandFootprints.cpp の FootCB（128 バイト）と一致させること
cbuffer FootCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float2 g_MapOrigin;
    float2 g_MapSize;
    float g_Strength;   // 足跡全体の強さ（0..1）
    float g_WetMul;     // 湿った砂ではこの倍数だけ深くなる
    float g_HasState;   // 1 = g_State がバインド済み（SandField）
    float g_Raise;      // 地面からの浮かせ量（m）
    float3 g_SunDir;    // ワールド空間、太陽の方向
    float g_Depth;      // m: へこみの深さ（壁面の明暗に効く）
    float g_Berm;       // m: 足跡の周りに押し上げられた砂の高さ
    float g_Grain;      // 0..1 砂粒ノイズ
    float g_SunGain;    // 壁面が太陽を受ける / 失う度合い
    float g_FloorDark;  // 底の踏み固められた砂をこれだけ暗くする
};

// 板は靴底よりこれだけ大きい（周りの砂の盛り上がりの分）
#define FOOT_CARD_PAD 1.4

struct FootOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;       // 靴底空間: -1..1 = 靴底、x = 横（左足は反転）、y = 縦（+1 = つま先）
    float alpha : TEXCOORD1;
    float3 sun : TEXCOORD2;      // 足跡座標系での太陽: x = 横（反転あり）、y = 縦、z = 上
    float2 halfSize : TEXCOORD3; // m: 靴底の半幅・半長
    float2 grainUV : TEXCOORD4;  // m、ワールド xz（砂粒は足跡と一緒に動かない）
};

#endif
