// ============================================================
// SwarmCorpseVS.hlsl
// Death shatter (2026-10-02): a dead enemy's rigid parts (Kenney Blocky:
// head / torso / arms / legs, one submesh each) fly apart, spin, bounce
// once on the ground and shrink away over g_CorpseLife seconds.
// Drawn like SwarmEnemyVS (same mesh, same PS, one submesh per draw, the
// corpse list of each look via DrawIndexedInstancedIndirect), but every
// part moves on its own closed-form path of (seed, part, age):
//
//   launch : away from the player (corpse.dir) x g_Fling, out from the
//            body along the part's own side, a random sideways push, and
//            up x g_Up. Elites / the boss throw a little harder
//   flight : ballistic under g_Gravity until the part's pivot reaches its
//            resting height, then one bounce (g_Bounce of the impact speed,
//            half the sideways speed), then it lies there
//   spin   : around a random axis, slowing to a third once it landed
//   end    : a white-hot flash for g_FlashTime, shrink from
//            g_ShrinkStart x life to the end
//
// The part pivot (g_PartPivot) is the part's node origin in the baked
// pose (a joint: neck, shoulder, hip), so a part tumbles around where it
// was attached.
//
// b0 = MVPBuffer (World unused), b2 = AICB (body size), b4 = CorpseCB,
// b5 = BomberCB (elite / boss scale, ghost look). C++: SwarmSystem::CorpseCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmCorpse> corpses : register(t0);
StructuredBuffer<uint> corpseList : register(t2);   // ring indices of this look (SwarmCorpseListCS)

static const uint kMaxCorpseParts = 8u;

cbuffer SwarmCorpseCB : register(b4)
{
    uint g_CorpsePart;        // submesh being drawn
    uint g_CorpsePartCount;
    float g_CorpseTime;       // SwarmSystem's clock
    float g_CorpseLife;       // seconds a corpse is drawn

    float g_CorpseGravity;    // m/s^2
    float g_CorpseFling;      // m/s away from the player
    float g_CorpseUp;         // m/s upward
    float g_CorpseSpin;       // rad/s

    float g_CorpseBounce;     // share of the impact speed kept by the bounce
    float g_CorpseFlash;      // vertex colour gain at the moment of death (HDR, bloom)
    float g_CorpseFlashTime;  // seconds the flash fades over
    float g_CorpseShrinkStart; // fraction of the life where shrinking starts

    float4 g_PartPivot[kMaxCorpseParts]; // xyz = pivot in the baked mesh space
};

// elite / boss / ghost tints, the same as SwarmEnemyVS
static const float3 kEliteTint = float3(1.4, 0.55, 0.5);
static const float3 kBossTint = float3(1.1, 0.5, 1.5);
static const float3 kGhostTint = float3(0.55, 1.25, 1.6);

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

uint CorpseHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float CorpseRand(uint seed, uint k)
{
    return (float) (CorpseHash(seed ^ (k * 0x85EBCA6Bu)) & 0xFFFFFFu) / 16777216.0;
}

// yaw = atan2(v.x, v.z): forward (0,0,1) -> (sin, 0, cos)
float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

// Rodrigues: rotate v by angle a around the unit axis k
float3 RotateAxis(float3 v, float3 k, float a)
{
    float s, c;
    sincos(a, s, c);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    o.Position = float4(0.0, 0.0, -1.0, 1.0);   // clipped unless it is still shattering

    SwarmCorpse cp = corpses[corpseList[input.InstanceID]];
    float age = g_CorpseTime - cp.birth;
    if (cp.birth <= 0.0 || age < 0.0 || age >= g_CorpseLife)
        return o;

    float ys, yc;
    sincos(cp.yaw, ys, yc);
    float kindScale = SwarmKindScale(cp.kind);
    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);

    // ---- the part in the baked mesh space, elite / boss scaled at the feet ----
    float3 local = input.Position;
    float3 pivot = g_PartPivot[min(g_CorpsePart, kMaxCorpseParts - 1u)].xyz;
    local.xz *= kindScale;
    local.y = footY + (local.y - footY) * kindScale;
    pivot.xz *= kindScale;
    pivot.y = footY + (pivot.y - footY) * kindScale;

    // ---- launch (world) ----
    uint seed = CorpseHash(cp.seed ^ (g_CorpsePart * 0x9E3779B9u));
    float r0 = CorpseRand(seed, 0u), r1 = CorpseRand(seed, 1u), r2 = CorpseRand(seed, 2u);
    float r3 = CorpseRand(seed, 3u), r4 = CorpseRand(seed, 4u), r5 = CorpseRand(seed, 5u);
    float3 away = float3(cp.dir.x, 0.0, cp.dir.y);
    float3 side = float3(-cp.dir.y, 0.0, cp.dir.x);
    float3 pivotW = RotateY(pivot, ys, yc);
    float outLenSq = dot(pivotW.xz, pivotW.xz);
    float3 outward = (outLenSq > 1e-4) ? float3(pivotW.x, 0.0, pivotW.z) * rsqrt(outLenSq) : float3(0.0, 0.0, 0.0);
    float throwMul = lerp(1.0, kindScale, 0.3);
    float3 vh = (away * (g_CorpseFling * (0.6 + 0.8 * r0))
               + outward * (1.0 + 1.5 * r1)
               + side * ((r2 - 0.5) * 2.0)) * throwMul;
    float vy = g_CorpseUp * (0.7 + 0.6 * r3) * throwMul;

    // ---- flight: ballistic until the pivot is back at its resting height, one bounce ----
    float g = max(g_CorpseGravity, 0.1);
    float restH = 0.12 * kindScale;                   // a part lying on the ground: pivot this high
    float h0 = max((pivot.y - footY) - restH, 0.0);   // pivot height above that at the moment of death
    float t1 = (vy + sqrt(vy * vy + 2.0 * g * h0)) / g;   // first impact
    float dy, hx, spinT;
    if (age < t1)
    {
        dy = vy * age - 0.5 * g * age * age;
        hx = age;
        spinT = age;
    }
    else
    {
        float vb = (g * t1 - vy) * g_CorpseBounce;    // impact speed x bounce, now upward
        float tb = age - t1;
        float t2 = 2.0 * vb / g;                       // the bounce's air time
        float tt = min(tb, t2);
        dy = -h0 + vb * tt - 0.5 * g * tt * tt;
        hx = t1 + 0.5 * tt;                            // half the sideways speed while bouncing, then it lies
        spinT = t1 + (age - t1) * 0.3;
    }
    float3 offset = vh * hx + float3(0.0, dy, 0.0);

    // ---- spin around a random axis, about the part's pivot ----
    float3 axis = normalize(float3(r4 - 0.5, 0.35, r5 - 0.5) + float3(1e-3, 0.0, 0.0));
    float angle = g_CorpseSpin * (0.5 + r0) * spinT * ((r1 < 0.5) ? -1.0 : 1.0);
    float shrink = 1.0 - smoothstep(g_CorpseShrinkStart * g_CorpseLife, g_CorpseLife, age);

    float3 rel = RotateAxis(local - pivot, axis, angle) * shrink;
    float3 worldPos = RotateY(pivot + rel, ys, yc) + cp.position + offset;
    float3 normal = RotateY(RotateAxis(input.Normal, axis, angle), ys, yc);
    float3 tangent = RotateY(RotateAxis(input.Tangent, axis, angle), ys, yc);

    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normalize(normal);
    o.Tangent = tangent;
    o.UV = input.UV;
    o.Color = input.Color;
    if (cp.kind == SWARM_KIND_ELITE)
        o.Color.rgb *= kEliteTint;
    else if (cp.kind == SWARM_KIND_BOSS)
        o.Color.rgb *= kBossTint;
    else if (cp.kind == SWARM_KIND_GHOST)
    {
        o.Color.rgb *= kGhostTint * g_GhostGlow;
        o.Color.a = g_GhostAlpha * shrink;
    }
    // the moment of death: white-hot, fading to normal (PS multiplies albedo by Color; > 1 = bloom)
    o.Color.rgb *= lerp(g_CorpseFlash, 1.0, saturate(age / max(g_CorpseFlashTime, 1e-3)));
    return o;
}
