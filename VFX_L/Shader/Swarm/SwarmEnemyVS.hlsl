// ============================================================
// SwarmEnemyVS.hlsl
// Instanced mob mesh. Position / yaw come from the enemy buffer,
// dead slots are collapsed behind the near plane.
//
// Uses ModelCommon's MVPBuffer on b0 (World is identity; the C++
// side writes it). SwarmCommon's cbuffers are moved off b0.
// enemies / enemyStates share t0 / t1 with ModelCommon's textures,
// which a VS never references.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
// live slot indices (SwarmEnemyCompactCS). InstanceID indexes this, not the pool
StructuredBuffer<uint> aliveList : register(t2);

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

// walk sway tuning (see main). Feet sit at local y = -0.9 (capsule bottom)
static const float kWalkFreq = 9.0;    // rad/s of walk clock -> ~1.4 steps/s
static const float kWalkBounce = 0.06; // meters
static const float kWalkRoll = 0.10;   // radians at the head

// yaw = atan2(v.x, v.z): forward (0,0,1) -> (sin, 0, cos)
float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;

    // InstanceID -> pool slot. The state check stays as a guard for the
    // debug path / stale lists (the list is rebuilt every frame)
    uint slot = aliveList[input.InstanceID];
    if (enemyStates[slot] == SWARM_DEAD)
    {
        o.Position = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float s, c;
    sincos(e.yaw, s, c);

    // ---- procedural walk: the mesh is one baked frame, so fake the gait ----
    // animTime is the per-enemy walk clock (MoveCS advances it while moving,
    // holds it during hit stun). Bounce up on each step and roll side to side,
    // stronger toward the head (local y). InstanceID offsets the phase so the
    // crowd does not march in lockstep.
    float3 local = input.Position;
    if (e.animIndex != 2u && dot(e.velocity.xz, e.velocity.xz) > 0.01)
    {
        float phase = e.animTime * kWalkFreq + (float) slot * 0.37;
        float bounce = abs(sin(phase)) * kWalkBounce;
        float roll = sin(phase) * kWalkRoll * saturate(local.y + 0.9); // 0 at the feet
        float rs, rc;
        sincos(roll, rs, rc);
        local.xy = float2(local.x * rc - local.y * rs, local.x * rs + local.y * rc);
        local.y += bounce;
    }

    float3 worldPos = RotateY(local, s, c) + e.position;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = RotateY(input.Normal, s, c);
    o.Tangent = RotateY(input.Tangent, s, c);
    o.UV = input.UV;
    o.Color = input.Color;

    // ---- hit flash: overbright vertex color during the stun, decays to 1 ----
    // PS multiplies albedo by Color, so > 1 goes HDR and bloom picks it up
    if (e.animIndex == 2u)
    {
        float t = saturate(e.animTime / max(g_HitStun, 1e-4));
        o.Color.rgb *= lerp(g_HitFlash, 1.0, t);
    }
    return o;
}