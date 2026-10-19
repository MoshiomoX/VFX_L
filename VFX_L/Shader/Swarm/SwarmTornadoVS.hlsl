// ============================================================
// SwarmTornadoVS.hlsl
// The sand mage's tornado (SWARM_KIND_TORNADO, 2026-10-17): an enemy
// slot that is really a projectile. Rewritten 2026-10-19 (the user:
// "it does not feel like wind at all"): the body is now the procedural
// shell from PrimitiveBuilder::CreateTornadoShell (y 0..1, radius 1,
// wide at both ends and a little narrower at the waist, uv.x spirals
// with the height). The wind look comes from the pixel shader scrolling
// two noise textures up the spiral (SwarmTornadoPS); this VS only
//   - scales the shell to the hit size (width) and tornadoLook.height,
//   - pushes the vertices in / out with a slow value noise so the
//     surface heaves instead of turning like a solid,
//   - spins slowly, grows out of the ground when born and shrinks away
//     as the life runs out,
//   - is drawn twice: layer 0 = the wide outer veil, layer 1 = the
//     narrower, faster, denser core (g_HoldOffset.y).
// Drawn translucent (premultiplied alpha, depth read only, no stencil)
// from RenderOverlay. Vertex colour alpha carries the end fade baked
// into the shell.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> tornadoList : register(t2);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// C++ mirror: SwarmSystem::PropMeshCB (same block as SwarmShieldVS, fields reused)
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor; // unused (the shell is built on its feet: y 0..1)
    float g_MeshScale;   // height in metres (outer layer)
    float3 g_HoldOffset; // x = width in metres (diameter of the outer layer at the top), y = layer (0 outer / 1 core), z = vertex noise amplitude (fraction of the radius)
    float g_MeshYaw;     // time in seconds (drives the heave here and the scrolling in the PS)
    float4 g_ShieldTint; // sand colour, a = overall opacity
    float g_BobAmount;   // spin rad/s
    float g_PropAlpha;   // seconds to grow out of the ground / shrink away at the end
    float g_HazeAlpha;   // layer 2 (sand haze): opacity at the foot (SwarmSystem::tornadoLook.sandAmount above 0.5)
    float _propPad;
};

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

// cheap 2D value noise (lattice hash, smooth interpolation). Tiles over the
// integer lattice, so it is sampled in (uv x k, uv y x k) space
float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}
float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1, 0));
    float c = Hash21(i + float2(0, 1));
    float d = Hash21(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = tornadoList[input.InstanceID];
    SwarmEnemyExtra ex = enemyExtra[slot];
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_TORNADO)
    {
        o.Position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];
    // animTime counts up from the spawn (MoveCS), fuse counts down to the end (AICS)
    float grow = max(g_PropAlpha, 1e-3);
    float s = saturate(e.animTime / grow) * saturate(max(ex.fuse, 0.0) / grow);

    // layer 0 = outer veil, 1 = core, 2 = sand haze (a soft film just inside the veil; 2026-10-19)
    float layerId = g_HoldOffset.y;
    bool haze = layerId > 1.5;
    float layer = haze ? 0.0 : saturate(layerId);
    float width = g_HoldOffset.x * (haze ? 0.82 : lerp(1.0, 0.55, layer));
    float height = g_MeshScale * (haze ? 0.9 : lerp(1.0, 0.94, layer));
    float t = g_MeshYaw + (float) slot * 0.37;   // every tornado heaves / scrolls in its own phase

    // ---- heave: push the ring in / out with a noise that drifts up the spiral ----
    float2 uv = input.UV;
    float speed = lerp(1.0, 1.5, layer);
    float nv = ValueNoise(float2(uv.x * 3.0 + t * 0.4, uv.y * 2.5 - t * 1.2 * speed));
    float d = 1.0 + (nv - 0.5) * 2.0 * g_HoldOffset.z * (haze ? 0.5 : 1.0);
    float3 local = float3(input.Position.x * d, input.Position.y, input.Position.z * d)
                 * float3(width * 0.5, height, width * 0.5) * s;

    // ---- slow spin (the texture does the real motion) ----
    float angle = -t * g_BobAmount * lerp(1.0, 1.4, layer) + (float) slot * 1.7;
    float sn, cs;
    sincos(angle, sn, cs);
    float3 p = float3(local.x * cs + local.z * sn, local.y, -local.x * sn + local.z * cs);
    float3 n = float3(input.Normal.x * cs + input.Normal.z * sn, input.Normal.y, -input.Normal.x * sn + input.Normal.z * cs);

    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float3 worldPos = e.position + float3(0.0, footY, 0.0) + p;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = n;
    o.Tangent = float3(haze ? 2.0 : layer, nv, t);   // read by SwarmTornadoPS (2 = haze)
    o.UV = uv;
    // the haze carries its own opacity (sandAmount) instead of the tint's
    o.Color = float4(g_ShieldTint.rgb, input.Color.a * (haze ? g_HazeAlpha : g_ShieldTint.a));
    return o;
}
