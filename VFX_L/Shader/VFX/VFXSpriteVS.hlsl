// ============================================================
// VFXSpriteVS.hlsl
// Sprite entry (CPU). The CPU writes one SpriteQuad per sprite into a
// dynamic structured buffer; draws are grouped by texture, g_First is
// the group's first quad (SV_InstanceID starts at 0 for every draw).
// ============================================================
#include "../Common/SpriteQuad.hlsli"

cbuffer SpriteCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamRight;
    uint g_First;
    float3 g_CamUp;
    float _pad0;
    float3 g_CamForward;
    float _pad1;
};

StructuredBuffer<SpriteQuad> g_Quads : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    nointerpolation uint flags : TEXCOORD1;
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    SpriteQuad q = g_Quads[g_First + iid];
    SpriteCamera cam;
    cam.right = g_CamRight;
    cam.up = g_CamUp;
    cam.forward = g_CamForward;

    float3 world;
    float2 uv;
    SpriteCorner(q, vid, cam, world, uv);

    VSOut o;
    o.position = mul(float4(world, 1.0), g_ViewProj);
    o.uv = uv;
    o.color = q.color;
    o.flags = q.flags;
    return o;
}
