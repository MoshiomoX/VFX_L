// ============================================================
// VFXLiquidVS.hlsl
// Liquid エントリの CPU 経路（2026-10-02）：このフレームに VFXLiquidEntry が
// 出した（VFXLiquidRenderer）インスタンス 1 つにつき水たまり 1 つ。地面のグリッドは
// LIQUID_GRID^2 枚の四角形で、SV_VertexID から作る。頂点バッファなし。
//
// 地形がある時（戦闘シーンが VFXLiquidRenderer に群れの高さ場 +
// そのグリッドの定数を渡す）は全頂点を高さ場の上に置き、中心自身の高さから
// 定義の cliff 以上ずれた地面は切り落とす（LiquidPlaceVertex）。
// 地形が無い時（エディタ）はグリッドをインスタンスの高さで
// 平らに置く。
//
// b0 = LiquidCameraCB、b1 = SwarmFrameCB（SwarmTerrainHeight 用の
// グリッド定数）、t0 = インスタンス、t1 = LiquidDef の表、t3 = 高さ
// インスタンスの C++ 側の対応：VFXLiquidInstance（VFXLiquidRenderer.h）
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"
#include "../Common/LiquidCommon.hlsli"

cbuffer LiquidCameraCB : register(b0)
{
    row_major float4x4 g_LViewProj;
    uint g_LHasTerrain;
    float3 _liquidCamPad;
};

struct LiquidInstance
{
    float3 position;   // 中心（ワールド）。地形が無い時はその y が地面
    float radius;      // m
    float2 dir;        // xz での投げた向き（0 = seed から決める角度）
    float age;         // 始まってからの s
    float left;        // 残りの s（大きい = 乾かない）
    uint def;          // LiquidDef の表の番号
    uint seed;
    float2 _pad;
};

StructuredBuffer<LiquidInstance> instances : register(t0);
StructuredBuffer<LiquidDef> liquidDefs : register(t1);
StructuredBuffer<float> heights : register(t3);

LiquidVSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    LiquidVSOut o = (LiquidVSOut) 0;
    LiquidInstance inst = instances[iid];
    LiquidDef L = liquidDefs[inst.def];

    float2 fwd, lft;
    LiquidFrame(inst.dir, inst.seed, fwd, lft);
    float R = inst.radius;
    // グリッドは液体の届く範囲 + その外のハローを覆う
    float extent = R * L.fill + max(L.haloWidth, 0.0) + 0.05;
    float2 lp = LiquidGridCorner(vid) * extent;
    float2 xz = inst.position.xz + fwd * lp.x + lft * lp.y;

    float ground = inst.position.y;
    float h = ground;
    if (g_LHasTerrain != 0u)
    {
        ground = SwarmTerrainHeight(heights, inst.position.xz);
        h = SwarmTerrainHeight(heights, xz);
    }
    LiquidPlaceVertex(L, ground, h, xz, lp, g_LViewProj, o);
    o.info = float4(R, inst.age, inst.left, 0.0);
    o.frame = float4(fwd, lft);
    o.ids = uint2(inst.def, inst.seed);
    return o;
}
