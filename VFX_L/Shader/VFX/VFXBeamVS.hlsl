// ============================================================
// VFXBeamVS.hlsl
// Beam entry (CPU). One instance = one layer of one beam
// (instance / 3 = beam, instance % 3 = layer). SV_VertexID builds a
// ribbon of BEAM_SEGMENTS quads from start to end, turned toward the
// camera around the beam axis. uv.x = distance along the beam (m),
// uv.y = -1..1 across.
// ============================================================
#include "../Common/BeamCommon.hlsli"

cbuffer BeamCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;
    uint g_First;
    float g_Time;
    float3 _pad0;
};

StructuredBuffer<BeamItem> g_Beams : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint beam : TEXCOORD1;
    nointerpolation uint layer : TEXCOORD2;
    nointerpolation float len : TEXCOORD3;
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o;
    o.beam = g_First + iid / BEAM_LAYERS;
    o.layer = iid % BEAM_LAYERS;
    BeamItem b = g_Beams[o.beam];

    static const float2 corners[6] =
    {
        float2(0, -1), float2(1, -1), float2(0, 1),
        float2(0, 1), float2(1, -1), float2(1, 1),
    };
    uint seg = vid / 6;
    float2 c = corners[vid % 6];
    float t = (seg + c.x) / (float) BEAM_SEGMENTS;

    float3 axis = b.end - b.start;
    float len = length(axis);
    axis = (len > 1e-4) ? axis / len : float3(0, 0, 1);
    float d = t * len;
    float3 p = b.start + axis * d;

    // ribbon faces the camera around the axis
    float3 toCam = g_CamPos - p;
    float3 side = cross(axis, toCam);
    float sl = length(side);
    side = (sl > 1e-4) ? side / sl : float3(0, 1, 0);

    float hw = BeamHalfWidth(b, o.layer, d, len);
    float3 world = p + side * (hw * c.y);

    o.position = mul(float4(world, 1.0), g_ViewProj);
    o.uv = float2(d, c.y);
    o.len = len;
    return o;
}
