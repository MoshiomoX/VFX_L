// ============================================================
// StaticPropVS.hlsl - 動かない置物（StaticPropRenderer）用の、
// 標準モデル VS のインスタンス描画版。
// VS_INPUT / VS_OUTPUT は VS.hlsl と同じなので、モデル自身の PS
// （PS.hlsl / PBR_PS.hlsl）をそのまま使える。
//   b0 MVPBuffer      : View / Projection（World は使わない）
//   b1 PropInstanceCB : この draw のインスタンスの開始位置
//   t8 見えているインスタンスのワールド行列（行ベクトル
//      規約、float4 1 つが 1 行、C++ の Matrix と同じ）
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
    uint g_FirstInstance;   // SV_InstanceID には StartInstanceLocation が含まれない
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
    // 接線の無いメッシュ（ゼロベクトル）は NaN にせず 0 のままにする（ModelVS 参照）
    float3 tw = mul(input.Tangent, (float3x3) world);
    o.Tangent = (dot(tw, tw) > 1e-12) ? normalize(tw) : float3(0.0, 0.0, 0.0);
    o.UV = input.UV;
    o.Color = input.Color;
    return o;
}
