// ============================================================
// StaticPropVS.hlsl - instanced version of the standard model VS
// for props that never move (StaticPropRenderer).
// Same VS_INPUT / VS_OUTPUT as VS.hlsl, so the model's own PS
// (PS.hlsl / PBR_PS.hlsl) is used unchanged.
//   b0 MVPBuffer      : View / Projection (World is unused)
//   b1 PropInstanceCB : where this draw's instances start
//   t8 world matrices of the visible instances (row-vector
//      convention, one row per float4, same as C++ Matrix)
// ============================================================
#include "Common/ModelCommon.hlsli"

struct PropInstance
{
    float4 r0;
    float4 r1;
    float4 r2;
    float4 r3;
};
StructuredBuffer<PropInstance> g_PropInstances : register(t8);

cbuffer PropInstanceCB : register(b1)
{
    uint g_FirstInstance;   // SV_InstanceID does not include StartInstanceLocation
    uint3 g_PropPad;
};

VS_OUTPUT main(VS_INPUT input, uint instanceId : SV_InstanceID)
{
    PropInstance inst = g_PropInstances[g_FirstInstance + instanceId];
    float4x4 world = float4x4(inst.r0, inst.r1, inst.r2, inst.r3);

    VS_OUTPUT o;
    float4 worldPos = mul(float4(input.Position, 1.0), world);
    o.WorldPos = worldPos.xyz;
    o.Position = mul(mul(worldPos, View), Projection);
    o.Normal = normalize(mul(input.Normal, (float3x3) world));
    o.Tangent = normalize(mul(input.Tangent, (float3x3) world));
    o.UV = input.UV;
    o.Color = input.Color;
    return o;
}
