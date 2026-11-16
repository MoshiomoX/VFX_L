// ============================================================
// TreeLeafShadowPS.hlsl
// 影の段の葉：形の外を捨てるだけ（深度は TreeLeafVS の位置のまま）
// ============================================================
#include "TreeLeafShape.hlsli"

struct LeafOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint seed : TEXCOORD3;
    nointerpolation float shade : TEXCOORD4;
};

void main(LeafOut i)
{
    if (abs(i.uv.y) > TreeLeafWidth(i.uv.x))
        discard;
}
