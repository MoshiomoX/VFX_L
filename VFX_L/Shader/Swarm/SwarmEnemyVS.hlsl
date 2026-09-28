// ============================================================
// SwarmEnemyVS.hlsl
// Instanced mob mesh. Position / yaw come from the enemy buffer,
// dead slots are collapsed behind the near plane.
//
// Uses ModelCommon's MVPBuffer on b0 (World is identity; the C++
// side writes it). SwarmCommon's cbuffers are moved off b0.
// enemies / enemyStates share t0 / t1 with ModelCommon's textures,
// which a VS never references.
//
// Motion: with a part animation table (b4 / t3, see SwarmEnemyAnimCB)
// the parts play idle / walk / attack. Without one (skinned models
// baked to one frame, e.g. KayKit Minion) the old procedural sway runs.
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
// live slot indices (SwarmEnemyCompactCS). InstanceID indexes this, not the pool.
// The C++ side binds the list of the kind being drawn (mobs, then bombers)
StructuredBuffer<uint> aliveList : register(t2);
// kind + fuse per slot: a lit bomber blinks faster and faster and swells up
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// bomber blink: starts at this many blinks per second, ends at the second value
static const float kFuseBlinkStart = 3.0;
static const float kFuseBlinkEnd = 14.0;

// ---- rigid part animation (models whose parts hang off animated nodes,
// e.g. Kenney Blocky: 6 rigid parts, no skin weights) ----
// Table layout: [clip][frame][part]. Each entry is a row-vector affine
// matrix taking a vertex from the BAKED pose of that part to the pose of
// that frame, so the mesh can stay the one baked frame. The C++ side
// draws one submesh at a time and writes g_AnimPart before each draw.
// Clips: 0 idle, 1 walk, 2 melee attack. C++ mirror: SwarmSystem::EnemyAnimCB
cbuffer SwarmEnemyAnimCB : register(b4)
{
    uint g_AnimPart;       // submesh being drawn
    uint g_AnimPartCount;
    uint g_AnimEnabled;    // 0 = no table: fall back to the procedural sway
    uint _animPad;
    uint4 g_ClipStart;     // first frame of each clip in the table
    uint4 g_ClipFrames;
    float4 g_ClipLength;   // seconds
    float g_AnimTime;      // clock for idle (seconds)
    float g_WalkRate;      // walk playback speed
    float2 _animPad2;
};

struct PartXform
{
    float4 r0;
    float4 r1;
    float4 r2;
    float4 r3;
};
StructuredBuffer<PartXform> partAnim : register(t3);

float3 XformPoint(PartXform m, float3 p)
{
    return p.x * m.r0.xyz + p.y * m.r1.xyz + p.z * m.r2.xyz + m.r3.xyz;
}
float3 XformDir(PartXform m, float3 d)
{
    return d.x * m.r0.xyz + d.y * m.r1.xyz + d.z * m.r2.xyz;
}

// two neighbouring frames of clip c at time t (seconds) and the blend
// weight between them. Looping clips wrap, the attack clamps at its end
void SamplePart(uint c, float t, bool loop, out PartXform a, out PartXform b, out float w)
{
    uint frames = max(g_ClipFrames[c], 1u);
    float u = t / max(g_ClipLength[c], 1e-3);
    u = loop ? frac(u) : saturate(u);
    float f = u * (float) (loop ? frames : max(frames - 1u, 1u));
    uint f0 = min((uint) floor(f), frames - 1u);
    uint f1 = loop ? (f0 + 1u) % frames : min(f0 + 1u, frames - 1u);
    w = f - floor(f);
    a = partAnim[(g_ClipStart[c] + f0) * g_AnimPartCount + g_AnimPart];
    b = partAnim[(g_ClipStart[c] + f1) * g_AnimPartCount + g_AnimPart];
}

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

    float3 local = input.Position;
    float3 normal = input.Normal;
    float3 tangent = input.Tangent;
    bool moving = dot(e.velocity.xz, e.velocity.xz) > 0.01;
    float phaseOffset = (float) slot * 0.37; // the crowd does not move in lockstep

    if (g_AnimEnabled != 0u)
    {
        // ---- rigid part animation: pick the clip from the enemy state ----
        //   attack : ContactCS just hit the player (attackCooldown was reset to
        //            g_AttackInterval and counts down), so the swing starts
        //            on the damage tick and plays once
        //   walk   : moving; animTime is the per-enemy clock MoveCS advances
        //   idle   : standing (blocked, or waiting at the player between hits)
        //   hit stun (animIndex 2) holds the first walk frame; the flash below still runs
        uint clip = 0u;
        float t = g_AnimTime + phaseOffset;
        bool loop = true;
        float sinceAttack = g_AttackInterval - e.attackCooldown;
        if (e.attackCooldown > 0.0 && sinceAttack < g_ClipLength.z)
        {
            clip = 2u;
            t = sinceAttack;
            loop = false;
        }
        else if (e.animIndex == 2u)
        {
            clip = 1u;
            t = phaseOffset;
        }
        else if (moving)
        {
            clip = 1u;
            t = e.animTime * g_WalkRate + phaseOffset;
        }

        PartXform fa, fb;
        float w;
        SamplePart(clip, t, loop, fa, fb, w);
        local = lerp(XformPoint(fa, input.Position), XformPoint(fb, input.Position), w);
        normal = normalize(lerp(XformDir(fa, input.Normal), XformDir(fb, input.Normal), w));
        tangent = lerp(XformDir(fa, input.Tangent), XformDir(fb, input.Tangent), w);
    }
    else if (e.animIndex != 2u && moving)
    {
        // ---- procedural walk: the mesh is one baked frame, so fake the gait ----
        // animTime is the per-enemy walk clock (MoveCS advances it while moving,
        // holds it during hit stun). Bounce up on each step and roll side to side,
        // stronger toward the head (local y)
        float phase = e.animTime * kWalkFreq + phaseOffset;
        float bounce = abs(sin(phase)) * kWalkBounce;
        float roll = sin(phase) * kWalkRoll * saturate(local.y + 0.9); // 0 at the feet
        float rs, rc;
        sincos(roll, rs, rc);
        local.xy = float2(local.x * rc - local.y * rs, local.x * rs + local.y * rc);
        local.y += bounce;
    }

    // ---- lit bomber: swell toward the blast, anchored at the feet ----
    SwarmEnemyExtra extra = enemyExtra[slot];
    bool lit = (extra.kind == SWARM_KIND_BOMBER) && (extra.fuse > 0.0);
    float fuseU = lit ? saturate(extra.fuse / max(g_BomberFuseTime, 1e-3)) : 0.0;
    if (lit)
    {
        float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
        float grow = 1.0 + g_BomberSwell * fuseU * fuseU;
        local.xz *= grow;
        local.y = footY + (local.y - footY) * grow;
    }

    float3 worldPos = RotateY(local, s, c) + e.position;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = RotateY(normal, s, c);
    o.Tangent = RotateY(tangent, s, c);
    o.UV = input.UV;
    o.Color = input.Color;

    // ---- hit flash: overbright vertex color during the stun, decays to 1 ----
    // PS multiplies albedo by Color, so > 1 goes HDR and bloom picks it up
    if (e.animIndex == 2u)
    {
        float t = saturate(e.animTime / max(g_HitStun, 1e-4));
        o.Color.rgb *= lerp(g_HitFlash, 1.0, t);
    }

    // ---- lit bomber: red-hot blink. The rate ramps linearly from start to
    // end, so the phase is its integral over the fuse time ----
    if (lit)
    {
        float T = max(g_BomberFuseTime, 1e-3);
        float f = extra.fuse;
        float phase = kFuseBlinkStart * f + 0.5 * (kFuseBlinkEnd - kFuseBlinkStart) / T * f * f;
        float on = (frac(phase) < 0.5) ? 1.0 : 0.0;
        float g = g_BomberFlashGain;
        o.Color.rgb *= lerp(float3(1, 1, 1), float3(g, g * 0.3, g * 0.2), on);
    }
    return o;
}