// ============================================================
// VFXLiquidVS.hlsl
// Liquid entry, CPU path (2026-10-02): one puddle per instance that
// VFXLiquidEntry submitted this frame (VFXLiquidRenderer), a ground grid of
// LIQUID_GRID^2 quads from SV_VertexID, no vertex buffer.
//
// With terrain (the battle scene hands VFXLiquidRenderer the swarm height
// field + its grid constants) every vertex sits on the height field, and
// ground off the centre's own height by more than the def's cliff is cut
// (LiquidPlaceVertex). Without terrain (the editors) the grid is flat at the
// instance height.
//
// b0 = LiquidCameraCB, b1 = SwarmFrameCB (grid constants for
// SwarmTerrainHeight), t0 = instances, t1 = LiquidDef table, t3 = heights
// C++ mirror of the instance: VFXLiquidInstance (VFXLiquidRenderer.h)
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
    float3 position;   // centre (world). Its y is the ground when there is no terrain
    float radius;      // m
    float2 dir;        // throw direction in xz (0 = an angle from the seed)
    float age;         // s since it started
    float left;        // s to go (large = no drying)
    uint def;          // index into the LiquidDef table
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
    // the grid covers the liquid's reach + the halo outside it
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
