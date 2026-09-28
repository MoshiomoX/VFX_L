// ============================================================
// SwarmOrbVS.hlsl
// Instanced exp orb gem (PrimitiveBuilder::CreateBipyramid).
// Draws every slot; dead ones are collapsed.
//
// Idle   : bobs up and down, spins about its long (Y) axis, breathes
//          (scale pulse). Each slot has its own phase so a pile of
//          orbs does not move in lockstep.
// Pulled : leans its long axis toward the player, stretches along the
//          pull (volume kept), stops bobbing, shifts to the pull color
//          and brightens. Amount = pull speed / g_FullPullSpeed.
//
// SwarmOrb.velocity is the horizontal pull written by SwarmOrbMoveCS
// (0 while idle). Color.rgb = body color, Color.a = glow multiplier;
// SwarmOrbPS does the shading.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmOrb> orbs : register(t0);
Buffer<uint> orbStates : register(t1);

// Must match SwarmSystem::OrbLookCB
cbuffer SwarmOrbLookCB : register(b4)
{
    float g_LookTime;       // seconds
    float g_LookScale;      // mesh scale
    float g_BobHeight;      // m
    float g_BobSpeed;       // rad/s

    float g_SpinSpeed;      // rad/s
    float g_PulseAmount;    // scale 1 +- this
    float g_PulseSpeed;     // rad/s
    float g_FullPullSpeed;  // m/s at which the pulled look is complete

    float g_StretchPerSpeed; // extra length per m/s of pull
    float g_StretchMax;      // cap on the extra length
    float g_TiltMax;         // radians the long axis leans toward the player
    float g_PullGlow;        // glow multiplier when fully pulled

    float4 g_IdleColor;     // linear rgb
    float4 g_PullColor;
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

// Rodrigues: rotate v about the unit axis a
float3 RotateAxis(float3 v, float3 a, float s, float c)
{
    return v * c + cross(a, v) * s + a * dot(a, v) * (1.0 - c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;

    if (orbStates[input.InstanceID] == SWARM_DEAD)
    {
        o.Position = float4(0, 0, -1, 1);
        return o;
    }

    SwarmOrb b = orbs[input.InstanceID];

    // per-slot phase
    uint h = input.InstanceID * 2654435761u;
    h ^= h >> 15;
    float phase = (float) (h & 0xFFFFu) * (6.2831853 / 65535.0);

    float attract = saturate(b._pad / max(g_FullPullSpeed, 1e-3));

    // ---- size: breathing ----
    float pulse = 1.0 + g_PulseAmount * sin(g_LookTime * g_PulseSpeed + phase * 1.7);
    float3 p = input.Position * (g_LookScale * pulse);
    float3 n = input.Normal;

    // ---- spin about the long axis ----
    float ss, sc;
    sincos(g_LookTime * g_SpinSpeed + phase, ss, sc);
    p = float3(sc * p.x + ss * p.z, p.y, -ss * p.x + sc * p.z);
    n = float3(sc * n.x + ss * n.z, n.y, -ss * n.x + sc * n.z);

    // ---- pulled: lean toward the player and stretch along the pull ----
    float2 v = b.velocity.xz;
    float vlen = length(v);
    if (vlen > 1e-3)
    {
        float3 d = float3(v.x, 0.0, v.y) / vlen;

        // axis = up x d, so +Y turns toward d
        float3 axis = float3(d.z, 0.0, -d.x);
        float ts, tc;
        sincos(g_TiltMax * attract, ts, tc);
        p = RotateAxis(p, axis, ts, tc);
        n = RotateAxis(n, axis, ts, tc);

        // k along d, 1/sqrt(k) across (keeps the volume);
        // normals take the inverse scale
        float k = 1.0 + min(vlen * g_StretchPerSpeed, g_StretchMax);
        float kp = rsqrt(k);
        float pd = dot(p, d);
        p = d * (pd * k) + (p - d * pd) * kp;
        float nd = dot(n, d);
        n = d * (nd / k) + (n - d * nd) / kp;
    }

    // ---- bob (idle only) ----
    float bob = sin(g_LookTime * g_BobSpeed + phase) * g_BobHeight * (1.0 - attract);

    float3 worldPos = b.position + p + float3(0.0, bob, 0.0);
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normalize(n);
    o.Tangent = float3(1, 0, 0);
    o.UV = input.UV;
    o.Color = float4(lerp(g_IdleColor.rgb, g_PullColor.rgb, attract),
                     lerp(1.0, g_PullGlow, attract));
    return o;
}
