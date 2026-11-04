// ============================================================
// SwarmLiquidVS.hlsl
// Liquid エントリの GPU 経路（2026-10-02）：レシピに Liquid エントリがある（毒沼）
// 生きた範囲の下に水たまりを描く。範囲の全スロットに DrawInstanced し、
// 死んだスロットや液体の無い範囲は潰して何も描かない。
// ピクセルは VFXLiquidPS（CPU 経路と同じ）。
//
//   recipe -> LiquidDef : recipeLiquid[a.vfxType] = def の番号 + 1（0 = なし）
//   age / direction     : liquidTrack[slot]（SwarmLiquidTrackCS）
//   radius              : def の radius × 範囲のサイズ倍率。def が 0 なら
//                         範囲の半径そのもの
//   seed                : スロット + 落ちた位置。同じスロットを使い回した
//                         2 つの沼が同じ見た目にならないように
//
// b0 = LiquidCameraCB、b1 = SwarmFrameCB（グリッドの定数）
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"
#include "../Common/LiquidCommon.hlsli"

cbuffer LiquidCameraCB : register(b0)
{
    row_major float4x4 g_LViewProj;
    uint g_LHasTerrain;   // ここでは常に 1
    float3 _liquidCamPad;
};

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
StructuredBuffer<float4> liquidTrack : register(t2);   // （向き x、向き z、最初に見た時の timeLeft、前フレームの timeLeft）
StructuredBuffer<float> heights : register(t3);
StructuredBuffer<uint> recipeLiquid : register(t4);
StructuredBuffer<LiquidDef> liquidDefs : register(t5);

LiquidVSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    LiquidVSOut o = (LiquidVSOut) 0;
    o.pos = float4(0.0, 0.0, -1.0, 1.0);   // 生きた液体でなければクリップされる

    if (areaStates[iid] == SWARM_DEAD)
        return o;
    SwarmArea a = areas[iid];
    uint li = recipeLiquid[a.vfxType];
    if (li == 0u)
        return o;
    float4 tr = liquidTrack[iid];
    if (tr.w <= 0.0)
        return o;   // まだトラッカーに見つかっていない

    LiquidDef L = liquidDefs[li - 1u];
    uint seed = LiquidHash(iid * 2654435761u ^ asuint(a.center.x) ^ (asuint(a.center.z) * 0x9E3779B9u));
    float2 fwd, lft;
    LiquidFrame(tr.xy, seed, fwd, lft);

    float R = (L.radius > 0.0) ? L.radius * SwarmAreaScale(a) : a.radius;
    float extent = R * L.fill + max(L.haloWidth, 0.0) + 0.05;
    float2 lp = LiquidGridCorner(vid) * extent;
    float2 xz = a.center.xz + fwd * lp.x + lft * lp.y;

    float ground = SwarmTerrainHeight(heights, a.center.xz);
    float h = SwarmTerrainHeight(heights, xz);
    LiquidPlaceVertex(L, ground, h, xz, lp, g_LViewProj, o);
    o.info = float4(R, max(tr.z - a.timeLeft, 0.0), a.timeLeft, 0.0);
    o.frame = float4(fwd, lft);
    o.ids = uint2(li - 1u, seed);
    return o;
}
