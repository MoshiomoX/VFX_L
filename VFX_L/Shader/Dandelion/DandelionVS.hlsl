// ============================================================
// DandelionVS.hlsl
// DrawInstanced(6, side^2 x (2 + g_SeedsPer)), no vertex buffer.
// Instance iid -> flower iid / (2 + seeds), part iid % (2 + seeds):
//   part 0 = the stem: a thin strip from the ground to the head,
//            turned to face the camera around its own axis, its top
//            leaning with the wind (Flower.bend)
//   part 1 = the seed head: a camera-facing disc (the PS cuts the
//            fluffy outline), its size = Flower.puff (0 after it was
//            blown apart, growing back)
//   part 2+ = one flying seed: a tiny camera-facing card
// Hidden flowers, empty heads, grounded / dead seeds and everything at
// the window's edge collapse to nothing.
// ============================================================
#include "DandelionCommon.hlsli"

StructuredBuffer<Flower> g_Flowers : register(t0);
StructuredBuffer<Seed> g_Seeds : register(t1);

struct DandelionOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;      // -1..1 across the card
    nointerpolation uint part : TEXCOORD3;   // 0 stem, 1 head, 2 seed
    nointerpolation uint seed : TEXCOORD4;
};

static const float2 kQuad[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

DandelionOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    uint perFlower = 2u + g_SeedsPer;
    uint fi = iid / perFlower;
    uint part = iid % perFlower;
    Flower f = g_Flowers[fi];

    DandelionOut o;
    o.uv = kQuad[vid];
    o.part = min(part, 2u);
    o.seed = f.seed + part * 977u;

    float scale = 1.0;
    float3 world;
    float3 n;
    if (part >= 2u)
    {
        // ---- a flying seed ----
        Seed z = g_Seeds[fi * g_SeedsPer + (part - 2u)];
        float grow = saturate(z.age / 0.15);
        float fade = saturate(z.life - z.age);   // the last second shrinks it away
        scale = (z.life > 0.0) ? grow * fade : 0.0;
        float s = g_SeedSize * g_SizeMul * scale;
        world = z.pos + (g_CamRight * o.uv.x + g_CamUp * o.uv.y) * s;
        // lit as if facing the sky as much as the camera: the fluff stays bright white instead of falling into the cool toon shade
        n = normalize(normalize(g_CamPos - z.pos) + float3(0.0, 1.5, 0.0));
    }
    else
    {
        float2 dc = abs(f.pos.xz - g_CamPos.xz);
        float edge = saturate((g_Window - max(dc.x, dc.y)) / max(g_FadeWidth, 0.01));
        scale = ((f.seed & DANDELION_HIDDEN) != 0u) ? 0.0 : edge;
        float stem = DandelionStemHeight(f.seed);
        float3 top = f.pos + float3(f.bend.x * stem, stem, f.bend.y * stem);
        if (part == 0u)
        {
            // ---- the stem: a strip from the ground to the head ----
            float t = o.uv.y * 0.5 + 0.5;
            float3 axis = top - f.pos;
            float3 toCam = normalize(g_CamPos - f.pos);
            float3 sideV = cross(axis, toCam);
            float sl = length(sideV);
            sideV = sl > 1e-4 ? sideV / sl : g_CamRight;
            float halfW = 0.018 * g_SizeMul * scale;
            // a little curve: the middle leans less than the top
            float3 along = f.pos + float3(f.bend.x * stem * t * t, stem * t, f.bend.y * stem * t * t);
            world = along + sideV * (o.uv.x * halfW);
            if (scale <= 0.0)
                world = f.pos;
            n = normalize(float3(toCam.x, 0.6, toCam.z));
        }
        else
        {
            // ---- the seed head ----
            float p = f.puff;
            float r = g_PuffRadius * g_SizeMul * scale * (p * p * (3.0 - 2.0 * p));
            world = top + (g_CamRight * o.uv.x + g_CamUp * o.uv.y) * r;
            n = normalize(g_CamPos - top);
            scale = r > 0.003 ? scale : 0.0;
            if (scale <= 0.0)
                world = top;
        }
    }

    o.world = world;
    o.normal = n;
    o.pos = mul(float4(world, 1.0), g_ViewProj);
    if (scale <= 0.0)
        o.pos = float4(0, 0, 0, 0);   // collapsed: no pixels
    return o;
}
