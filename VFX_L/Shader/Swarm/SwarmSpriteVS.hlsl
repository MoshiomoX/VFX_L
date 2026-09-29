// ============================================================
// SwarmSpriteVS.hlsl
// Draws every slot of the GPU sprite pool (6 vertices each); dead
// slots collapse to a clipped point. The frame comes from the age.
// Same camera cbuffer as VFXSpriteVS (g_First unused here).
// ============================================================
#include "../Common/SpriteQuad.hlsli"
#include "../Common/SwarmSprite.hlsli"

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

StructuredBuffer<SwarmSprite> sprites : register(t0);
StructuredBuffer<SwarmSpriteDef> spriteDefs : register(t1);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    nointerpolation uint flags : TEXCOORD1;
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o = (VSOut) 0;
    SwarmSprite s = sprites[iid];
    if (s.alive == 0u)
    {
        o.position = float4(0, 0, -1, 1); // behind the near plane: clipped
        return o;
    }

    SwarmSpriteDef d = spriteDefs[s.def];
    uint count = max(d.frameCount, 1u);
    uint cols = max(d.cols, 1u);
    uint f = (uint) (s.age / max(d.frameTime, 1e-4));
    f = ((d.flags & SWARM_SPRITE_LOOP) != 0u) ? (f % count) : min(f, count - 1u);

    SpriteQuad q;
    q.position = s.position;
    q.rotation = d.rotation;
    float scale = (s.sizeScale > 0.0) ? s.sizeScale : 1.0;   // Magnifier
    q.size = float2(d.height * d.aspect, d.height) * scale;
    q.pivot = d.pivot;
    q.uvRect = float4((float) (f % cols) * d.cellUV.x, (float) (f / cols) * d.cellUV.y, d.cellUV.x, d.cellUV.y);
    q.color = d.color;
    q.facing = d.facing;
    q.flags = d.flags;
    q._pad = float2(0, 0);

    SpriteCamera cam;
    cam.right = g_CamRight;
    cam.up = g_CamUp;
    cam.forward = g_CamForward;

    float3 world;
    float2 uv;
    SpriteCorner(q, vid, cam, world, uv);

    o.position = mul(float4(world, 1.0), g_ViewProj);
    o.uv = uv;
    o.color = q.color;
    o.flags = q.flags;
    return o;
}
