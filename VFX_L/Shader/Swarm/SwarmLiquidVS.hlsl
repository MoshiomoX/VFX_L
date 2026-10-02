// ============================================================
// SwarmLiquidVS.hlsl
// Liquid entry, GPU path (2026-10-02): a puddle under every live area whose
// recipe holds a Liquid entry (the Poison pool). DrawInstanced over all
// area slots; dead slots and areas without a liquid collapse to nothing.
// Pixels: VFXLiquidPS, the same as the CPU path.
//
//   recipe -> LiquidDef : recipeLiquid[a.vfxType] = def index + 1 (0 = none)
//   age / direction     : liquidTrack[slot] (SwarmLiquidTrackCS)
//   radius              : the def's radius x the area's size scale, or the
//                         area radius itself when the def says 0
//   seed                : the slot + where it landed, so two pools that
//                         reuse a slot do not look the same
//
// b0 = LiquidCameraCB, b1 = SwarmFrameCB (grid constants)
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"
#include "../Common/LiquidCommon.hlsli"

cbuffer LiquidCameraCB : register(b0)
{
    row_major float4x4 g_LViewProj;
    uint g_LHasTerrain;   // always 1 here
    float3 _liquidCamPad;
};

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
StructuredBuffer<float4> liquidTrack : register(t2);   // (dir x, dir z, timeLeft first seen, timeLeft last frame)
StructuredBuffer<float> heights : register(t3);
StructuredBuffer<uint> recipeLiquid : register(t4);
StructuredBuffer<LiquidDef> liquidDefs : register(t5);

LiquidVSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    LiquidVSOut o = (LiquidVSOut) 0;
    o.pos = float4(0.0, 0.0, -1.0, 1.0);   // clipped unless it is a live liquid

    if (areaStates[iid] == SWARM_DEAD)
        return o;
    SwarmArea a = areas[iid];
    uint li = recipeLiquid[a.vfxType];
    if (li == 0u)
        return o;
    float4 tr = liquidTrack[iid];
    if (tr.w <= 0.0)
        return o;   // not seen by the tracker yet

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
