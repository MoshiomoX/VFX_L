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
// kind + fuse per slot: a lit bomber blinks faster and faster and swells up,
// a charger blinks red while it winds up (2026-10-08)
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);
// slow + freeze per slot (z > 0 = frozen by the Ice Lance: the pose holds, the body turns icy)
StructuredBuffer<float4> enemySlow : register(t4);
// stored toxin per slot (x = damage still to drain, the poison pools, 2026-10-17): the body turns a sickly green
StructuredBuffer<float4> enemyPoison : register(t6);
static const float3 kPoisonTint = float3(0.55, 1.15, 0.45);

// bomber blink: starts at this many blinks per second, ends at the second value
static const float kFuseBlinkStart = 3.0;
static const float kFuseBlinkEnd = 14.0;
// charger wind-up blink, same ramp (blinks per second at the start / the end of the wind-up)
static const float kChargeBlinkStart = 4.0;
static const float kChargeBlinkEnd = 12.0;
// frozen: pale blue, times g_FreezeTint
static const float3 kIceTint = float3(0.75, 1.05, 1.7);
// elite: dark red tint on the shared zombie texture
static const float3 kEliteTint = float3(1.4, 0.55, 0.5);
// boss: g_BossTint (BomberCB, per stage: grassland violet, desert sandstone. 2026-10-24)
// ghost: translucent cyan-white, drawn with alpha blend (SwarmSystem draws its list last)
static const float3 kGhostTint = float3(0.55, 1.25, 1.6);
// dark ghost (2026-10-18): near-black violet, more opaque, with a slow purple pulse
static const float3 kDarkGhostTint = float3(0.10, 0.03, 0.24);

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
    uint g_AnimMode;       // 0 = moves with part g_AnimPart, 1 = a boss decoration orbiting the body axis (2026-10-24)
    uint4 g_ClipStart;     // first frame of each clip in the table
    uint4 g_ClipFrames;
    float4 g_ClipLength;   // seconds
    float g_AnimTime;      // clock for idle (seconds)
    float g_WalkRate;      // walk playback speed
    float g_OrbitSpeed;    // rad/s for g_AnimMode 1
    float g_Hunch;         // the werewolf's hunch (2026-10-27): rad the piece is pitched forward about the hips (g_Hinge.xyz) before the part's own motion; 0 = none
    float4 g_Flap;         // g_AnimMode 2 (a gargoyle's wing, 2026-10-25): hinge (baked mesh space) xyz, w = +1 right wing / -1 left
    float4 g_Hinge;        // g_AnimMode 4's second joint (2026-10-27, the werewolf's knee): xyz, w = bend rad while the leg lifts (0 = none). With g_Hunch: xyz = the hips
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
    SwarmEnemyExtra extra = enemyExtra[slot];
    float4 status = enemySlow[slot];
    bool frozen = status.z > 0.0;
    // a sleeping gargoyle is a statue: no motion at all (2026-10-25); the first part of its waking only shakes
    bool statue = (extra.kind == SWARM_KIND_GARGOYLE) && extra.fuse < 0.35 * g_GargoyleWakeTime;
    // ---- the thunder stag's charge (2026-10-26, g_BossChargePose from BossAttacks): 0..1 wind-up, 1 dashing, 1..1.3 the
    //      toss of the head on a hit, 1 -> 0 after. Only a quadruped boss gets a pose > 0 ----
    float cpose = (extra.kind == SWARM_KIND_BOSS) ? g_BossChargePose : 0.0;
    float headDown = min(cpose, 1.0);                 // the head lowered (antlers forward), antlers glowing
    float headToss = saturate((cpose - 1.0) / 0.3);   // thrown up on the hit
    float dashK = saturate((cpose - 0.85) / 0.15);    // 1 while dashing: lean, fast gait
    bool bracing = (extra.kind == SWARM_KIND_BOSS) && (g_BossChargeOn > 0.5) && (g_BossChargeSpeed <= 0.0);   // standing in a wind-up
    // the stage's fixed elites (2026-10-25): the CPU's flags (attack loop, spin, enraged) and its wind-up glow
    uint eliteFlags = 0u;
    float eliteGlow = 0.0;
    if (SwarmIsFixedElite(extra.kind))
    {
        int erow = SwarmEliteEntry(slot);
        if (erow >= 0)
        {
            eliteFlags = g_EliteCmd[erow].y;
            eliteGlow = (float) g_EliteCmd[erow].w / 1000.0;
        }
    }
    bool eliteAttack = (eliteFlags & SWARM_ELITE_ATTACK) != 0u;
    if ((eliteFlags & SWARM_ELITE_SPIN) != 0u)
        sincos(e.yaw + g_AnimTime * 15.0, s, c);   // the axe's whirl: the whole body turns

    // ---- a gargoyle's wing (g_AnimMode 2, 2026-10-25): turned about the forward axis through the hinge before the
    //      part's own animation. Hangs while a statue / landed, flaps in the air, swept up through the dive ----
    // the werewolf's hunch (g_Hunch != 0, 2026-10-27): deeper while it runs (applied below, after the limb swings)
    float hunchEff = 0.0;
    if (g_Hunch != 0.0)
        hunchEff = g_Hunch * (1.0 + 0.45 * ((frozen || statue) ? 0.0 : saturate(length(e.velocity.xz) / 2.5)));
    // a procedural biped (the werewolf, 2026-10-27): every piece has g_Hunch (the upper body) or a knee (the legs). It is not
    // moved by the Kenney part clips (the torso's bob and twist made the hunched body and the legs wobble); instead the whole
    // body sinks by what the planted leg's swing lifts it, so one foot stays on the ground (the hips are 0.43 above the feet)
    bool biped = (g_Hunch != 0.0) || (g_AnimMode == 4u && g_Hinge.w > 0.0);
    float bipedDrop = 0.0;
    if (biped && moving && !frozen)
    {
        float stride0 = e.animTime * g_WalkRate / max(g_ClipLength.y, 0.05);
        float legAng = 0.65 * (0.5 + 0.25 * dashK) * sin(stride0 * 6.2831853 * (1.0 + 0.8 * dashK));
        bipedDrop = 0.43 * (1.0 - cos(legAng));
    }
    float3 inPos = input.Position;
    float3 inNrm = input.Normal;
    float3 inTan = input.Tangent;
    if (g_AnimMode == 2u)
    {
        float ang = -0.25;
        if (SwarmGargoyleAirborne(extra))
        {
            if (extra.fuse >= SWARM_GARGOYLE_DIVE)
                ang = SwarmGargoyleDiving(extra) ? 0.95 : 0.6;
            else
                ang = 0.15 + g_GargoyleFlap * sin(g_AnimTime * 9.0 + phaseOffset);
        }
        float as, ac;
        sincos(ang * g_Flap.w, as, ac);
        float2 r = inPos.xy - g_Flap.xy;
        inPos.xy = g_Flap.xy + float2(r.x * ac - r.y * as, r.x * as + r.y * ac);
        inNrm.xy = float2(inNrm.x * ac - inNrm.y * as, inNrm.x * as + inNrm.y * ac);
        inTan.xy = float2(inTan.x * ac - inTan.y * as, inTan.x * as + inTan.y * ac);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- a scorpion's tail (g_AnimMode 3, 2026-10-26): pitched about the sideways axis through the hinge (g_Flap.xyz)
    //      before the torso's own animation. Sways slowly; whips forward over the head on the melee swing (g_Flap.w rad,
    //      same timing as the attack clip), then comes back ----
    if (g_AnimMode == 3u)
    {
        float ang = 0.12 * sin(g_AnimTime * 1.6 + phaseOffset);
        float sinceStab = g_AttackInterval - e.attackCooldown;
        if (e.attackCooldown > 0.0 && sinceStab < g_ClipLength.z)
        {
            float u = saturate(sinceStab / max(g_ClipLength.z, 0.05));
            ang += g_Flap.w * sin(u * 3.14159);
        }
        // the stag's charge: the head goes down for the wind-up and stays down through the dash, is thrown up on the hit
        ang += g_Flap.w * (headDown - 2.2 * headToss);
        float ts, tc;
        sincos(ang, ts, tc);
        float2 r = inPos.yz - g_Flap.yz;
        inPos.yz = g_Flap.yz + float2(r.x * tc - r.y * ts, r.x * ts + r.y * tc);
        inNrm.yz = float2(inNrm.y * tc - inNrm.z * ts, inNrm.y * ts + inNrm.z * tc);
        inTan.yz = float2(inTan.y * tc - inTan.z * ts, inTan.y * ts + inTan.z * tc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- a quadruped's leg (g_AnimMode 4, 2026-10-26, the stag): swung about the sideways axis through its own top
    //      (g_Flap.xyz) by the walk clock, phase g_Flap.w (front-left + hind-right 0, the other pair pi). Straight
    //      while standing. The torso's own animation (its bob) is applied after this ----
    if (g_AnimMode == 4u)
    {
        float ang = 0.0;
        float bend = 0.0;   // the werewolf's knee (g_Hinge.w > 0, 2026-10-27): folds while the leg swings forward (lifted)
        if (moving && !frozen)
        {
            float stride = e.animTime * g_WalkRate / max(g_ClipLength.y, 0.05);   // walk loops = strides
            // the charge (dashK): the legs swing faster and wider (a gallop)
            float ph = stride * 6.2831853 * (1.0 + 0.8 * dashK) + g_Flap.w;
            ang = (0.5 + 0.25 * dashK) * sin(ph);
            if (g_Hinge.w > 0.0)
                ang *= 0.65;   // a biped's leg (the werewolf): a shorter stride than a quadruped's
            bend = g_Hinge.w * max(0.0, -cos(ph));
        }
        else if (bracing)
        {
            // the wind-up: the front legs forward, the hind legs back (braced, g_Flap.z > 0 = a front leg)
            ang = 0.22 * headDown * ((g_Flap.z > 0.0) ? -1.0 : 1.0);
        }
        // a hunched biped's arm (g_Hunch != 0, the werewolf): turned back by half the hunch about the shoulder, so the
        // hand hangs beside the knee (the full hunch left the hands dangling far out in front, 2026-10-27). The walk
        // swing is 0.6 of a leg's (0.3 looked the same on both arms); the melee swing throws the left claw in the first
        // half of the clip and the right in the second (both at once looked like one arm)
        if (g_Hunch != 0.0)
            ang = ang * 0.6 - 0.5 * hunchEff;
        if (g_Hunch != 0.0 && e.attackCooldown > 0.0)
        {
            float sinceSwing = g_AttackInterval - e.attackCooldown;
            if (sinceSwing < g_ClipLength.z)
            {
                float u = saturate(sinceSwing / max(g_ClipLength.z, 0.05)) * 2.0 - ((g_Flap.x < 0.0) ? 0.0 : 1.0);
                ang -= 1.6 * sin(saturate(u) * 3.14159);
            }
        }
        if (bend > 0.0 && g_Hinge.w > 0.0 && inPos.y < g_Hinge.y)
        {
            // below the knee: the shin swings back about the knee (same sense as the hip)
            float ks, kc;
            sincos(bend, ks, kc);
            float2 rk = inPos.yz - g_Hinge.yz;
            inPos.yz = g_Hinge.yz + float2(rk.x * kc - rk.y * ks, rk.x * ks + rk.y * kc);
            inNrm.yz = float2(inNrm.y * kc - inNrm.z * ks, inNrm.y * ks + inNrm.z * kc);
            inTan.yz = float2(inTan.y * kc - inTan.z * ks, inTan.y * ks + inTan.z * kc);
        }
        float ls, lc;
        sincos(ang, ls, lc);
        float2 r = inPos.yz - g_Flap.yz;
        inPos.yz = g_Flap.yz + float2(r.x * lc - r.y * ls, r.x * ls + r.y * lc);
        inNrm.yz = float2(inNrm.y * lc - inNrm.z * ls, inNrm.y * ls + inNrm.z * lc);
        inTan.yz = float2(inTan.y * lc - inTan.z * ls, inTan.y * ls + inTan.z * lc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- an insect's leg (g_AnimMode 5, 2026-10-26, the scorpion): swept about the VERTICAL axis through its own root
    //      (g_Flap.xyz) by the walk clock, phase g_Flap.w (a tripod gait: alternate legs on each side). Lifted a little
    //      while it swings forward (the return stroke). +x legs sweep forward with a negative angle ----
    if (g_AnimMode == 5u)
    {
        float ang = 0.0;
        float lift = 0.0;
        if (moving && !frozen)
        {
            float stride = e.animTime * g_WalkRate / max(g_ClipLength.y, 0.05);
            float ph = stride * 6.2831853 * (1.0 + 0.8 * dashK) + g_Flap.w;
            ang = 0.38 * sin(ph) * ((g_Flap.x < 0.0) ? 1.0 : -1.0);
            lift = 0.10 * max(0.0, cos(ph));
        }
        float ls, lc;
        sincos(ang, ls, lc);
        float2 r = inPos.xz - g_Flap.xz;
        inPos.xz = g_Flap.xz + float2(r.x * lc + r.y * ls, -r.x * ls + r.y * lc);
        inPos.y += lift * saturate(abs(r.x) / 0.75);
        inNrm.xz = float2(inNrm.x * lc + inNrm.z * ls, -inNrm.x * ls + inNrm.z * lc);
        inTan.xz = float2(inTan.x * lc + inTan.z * ls, -inTan.x * ls + inTan.z * lc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- the werewolf's hunch (g_Hunch != 0, 2026-10-27): the piece (torso / head / arm, already swung above) is pitched
    //      forward about the hips (g_Hinge.yz) before the part's own motion. Deeper while it runs (the speed smooths it) ----
    if (g_Hunch != 0.0)
    {
        float hs, hc;
        sincos(hunchEff, hs, hc);
        float2 rh = inPos.yz - g_Hinge.yz;
        inPos.yz = g_Hinge.yz + float2(rh.x * hc - rh.y * hs, rh.x * hs + rh.y * hc);
        inNrm.yz = float2(inNrm.y * hc - inNrm.z * hs, inNrm.y * hs + inNrm.z * hc);
        inTan.yz = float2(inTan.y * hc - inTan.z * hs, inTan.y * hs + inTan.z * hc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    if (g_AnimMode == 1u)
    {
        // ---- boss decoration orbiting the body (the sand guardian's floating rocks, 2026-10-24): spin the whole
        //      ring around the vertical axis and bob it a little. Not frozen with the body: it is a spell, not a limb ----
        float a = g_AnimTime * g_OrbitSpeed;
        float os, oc;
        sincos(a, os, oc);
        local = float3(input.Position.x * oc + input.Position.z * os, input.Position.y + 0.06 * sin(g_AnimTime * 1.7),
                       -input.Position.x * os + input.Position.z * oc);
        normal = float3(input.Normal.x * oc + input.Normal.z * os, input.Normal.y, -input.Normal.x * os + input.Normal.z * oc);
        tangent = float3(input.Tangent.x * oc + input.Tangent.z * os, input.Tangent.y, -input.Tangent.x * os + input.Tangent.z * oc);
    }
    else if (biped)
    {
        // the procedural biped (above): no part clip, the body sinks with the stride and the upper body breathes a little
        local = inPos;
        normal = inNrm;
        tangent = inTan;
        local.y -= bipedDrop;
        if (g_Hunch != 0.0)
            local.y += 0.006 * sin(g_AnimTime * 2.2 + phaseOffset);
    }
    else if (g_AnimEnabled != 0u)
    {
        // ---- rigid part animation: pick the clip from the enemy state ----
        //   attack : ContactCS just hit the player (attackCooldown was reset to
        //            g_AttackInterval and counts down), so the swing starts
        //            on the damage tick and plays once
        //   walk   : moving; animTime is the per-enemy clock MoveCS advances
        //   idle   : standing (blocked, or waiting at the player between hits)
        //   hit stun (animIndex 2) and frozen hold a walk frame; the flash below still runs
        uint clip = 0u;
        float t = g_AnimTime + phaseOffset;
        bool loop = true;
        float sinceAttack = g_AttackInterval - e.attackCooldown;
        if (statue)
        {
            clip = 0u;
            t = 0.0;
        }
        else if (frozen)
        {
            clip = 1u;
            t = phaseOffset;
        }
        else if (eliteAttack)
        {
            // a fixed elite's move: the swing over and over
            clip = 2u;
            t = fmod(g_AnimTime * 1.3 + phaseOffset, max(g_ClipLength.z, 0.05));
            loop = false;
        }
        else if (e.attackCooldown > 0.0 && sinceAttack < g_ClipLength.z)
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
        local = lerp(XformPoint(fa, inPos), XformPoint(fb, inPos), w);
        normal = normalize(lerp(XformDir(fa, inNrm), XformDir(fb, inNrm), w));
        tangent = lerp(XformDir(fa, inTan), XformDir(fb, inTan), w);
    }
    else if (e.animIndex != 2u && moving && !frozen)
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

    // ---- the thunder stag's charge (2026-10-26): the whole body (every decoration passes through here) crouches a
    //      little in the wind-up and pitches forward about the hips while dashing. Hips = (y 0.5, z -0.6) in the baked
    //      mesh space (the hind legs' hinge after the stag's 1.15x scale) ----
    if (cpose > 0.0)
    {
        // the werewolf's pounce: a parabola of g_BossJumpHeight (world m; the kind scale is applied below, so divide it out)
        if (g_BossJumpHeight > 0.0)
        {
            float p = min(cpose, 1.0);
            local.y += g_BossJumpHeight * 4.0 * p * (1.0 - p) / max(SwarmKindScale(extra.kind), 1.0);
        }
        local.y -= 0.08 * headDown * (1.0 - dashK);
        // the pivot and angle come from the CPU per boss (g_BossLeanPivot* / g_BossLeanAngle: the stag's hips (0.5, -0.6) 0.17,
        // the scorpion's low body (-0.6, 0) 0.08. 0 = no lean)
        float lean = g_BossLeanAngle * dashK;
        float ps, pc;
        sincos(lean, ps, pc);
        float2 hips = float2(g_BossLeanPivotY, g_BossLeanPivotZ);
        float2 r = local.yz - hips;
        local.yz = hips + float2(r.x * pc - r.y * ps, r.x * ps + r.y * pc);
        normal.yz = float2(normal.y * pc - normal.z * ps, normal.y * ps + normal.z * pc);
        tangent.yz = float2(tangent.y * pc - tangent.z * ps, tangent.y * ps + tangent.z * pc);
    }

    // ---- elite: bigger body, anchored at the feet (SwarmKindCapsule matches) ----
    float kindScale = SwarmKindScale(extra.kind);
    if (kindScale != 1.0)
    {
        float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
        local.xz *= kindScale;
        local.y = footY + (local.y - footY) * kindScale;
    }

    // ---- fixed elite: bigger when enraged, shrinks into the ground as it vanishes (the shadow swordsman, 2026-10-25) ----
    if (SwarmIsFixedElite(extra.kind))
    {
        float hide = saturate(-extra.fuse);
        float grow = (((eliteFlags & SWARM_ELITE_ENRAGED) != 0u) ? g_EliteEnragedScale : 1.0) * (1.0 - hide);
        float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
        local.xz *= grow;
        local.y = footY + (local.y - footY) * grow;
    }

    // ---- lit bomber: swell toward the blast, anchored at the feet ----
    bool lit = SwarmInvulnerable(extra);   // a lit bomber / dash bomber (2026-10-18)
    float fuseT = (extra.kind == SWARM_KIND_DASHBOMBER) ? g_DashBomberWindup + g_DashBomberFuse : g_BomberFuseTime;
    float fuseU = lit ? saturate(extra.fuse / max(fuseT, 1e-3)) : 0.0;
    if (lit)
    {
        float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
        float grow = 1.0 + g_BomberSwell * fuseU * fuseU;
        local.xz *= grow;
        local.y = footY + (local.y - footY) * grow;
    }

    // ---- burrower: the body is under the sand while buried, rises out when it surfaces and sinks back (2026-10-17) ----
    if (extra.kind == SWARM_KIND_BURROWER)
    {
        float down = (extra.fuse > 0.0) ? 1.0 - saturate((g_BurrowSurfaceTime - extra.fuse) / SWARM_BURROW_RISE_TIME)
                                        : saturate(-extra.fuse / SWARM_BURROW_RISE_TIME);
        float bodyH = (g_EnemyRadius + g_EnemyCapsuleHalf) * 2.0 * kindScale + 0.6;
        local.y -= bodyH * down;
    }
    // ---- sand guardian burrowing (2026-10-24): fuse = -burrow 0..1 (AICS from g_BossBurrow), the body sinks the same way ----
    if (extra.kind == SWARM_KIND_BOSS && extra.fuse < 0.0)
    {
        float bodyH = (g_EnemyRadius + g_EnemyCapsuleHalf) * 2.0 * kindScale + 0.6;
        local.y -= bodyH * saturate(-extra.fuse);
    }

    float3 worldPos = RotateY(local, s, c) + e.position;
    // ghost: hovers and bobs (the gameplay position stays on the ground)
    if (SwarmIsGhost(extra.kind))
        worldPos.y += g_GhostHover + 0.15 * sin(g_AnimTime * 3.0 + (float) slot * 0.7);
    // a waking gargoyle shakes the stone off (2026-10-25)
    if (extra.kind == SWARM_KIND_GARGOYLE && extra.fuse > 0.0 && extra.fuse < 0.35 * g_GargoyleWakeTime)
    {
        float k = 0.06 * (1.0 - extra.fuse / (0.35 * g_GargoyleWakeTime + 1e-3));
        worldPos.x += k * sin(g_AnimTime * 61.0 + (float) slot);
        worldPos.z += k * sin(g_AnimTime * 53.0 + (float) slot * 1.7);
    }
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = RotateY(normal, s, c);
    o.Tangent = RotateY(tangent, s, c);
    o.UV = input.UV;
    o.Color = input.Color;
    if (extra.kind == SWARM_KIND_ELITE)
        o.Color.rgb *= kEliteTint;
    else if (extra.kind == SWARM_KIND_BOSS)
    {
        o.Color.rgb *= g_BossTint;
        // the thunder stag: its glowing parts (HDR vertex colour = the antler edges and eyes) flare up through the
        // wind-up and the dash (2026-10-26)
        float glowy = max(max(input.Color.r, input.Color.g), input.Color.b);
        if (glowy > 1.2)
            o.Color.rgb *= 1.0 + 1.0 * headDown;
    }
    else if (extra.kind == SWARM_KIND_GHOST)
    {
        o.Color.rgb *= kGhostTint * g_GhostGlow;
        o.Color.a = g_GhostAlpha;
    }
    else if (extra.kind == SWARM_KIND_DARKGHOST)
    {
        float pulse = 0.75 + 0.25 * sin(g_AnimTime * 2.2 + (float) slot * 1.3);
        o.Color.rgb *= kDarkGhostTint * pulse;
        o.Color.a = g_DarkGhostAlpha;
    }

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
        float T = max(fuseT, 1e-3);
        float f = extra.fuse;
        float phase = kFuseBlinkStart * f + 0.5 * (kFuseBlinkEnd - kFuseBlinkStart) / T * f * f;
        float on = (frac(phase) < 0.5) ? 1.0 : 0.0;
        float g = g_BomberFlashGain;
        o.Color.rgb *= lerp(float3(1, 1, 1), float3(g, g * 0.3, g * 0.2), on);
    }

    // ---- charger: red blink while winding up (faster toward the dash), reddish while dashing ----
    if (extra.kind == SWARM_KIND_CHARGER)
    {
        uint ph = SwarmChargerPhase(extra.fuse);
        if (ph == SWARM_CHARGE_WINDUP)
        {
            float T = max(g_ChargerWindup, 1e-3);
            float f = extra.fuse;
            float phase = kChargeBlinkStart * f + 0.5 * (kChargeBlinkEnd - kChargeBlinkStart) / T * f * f;
            float on = (frac(phase) < 0.5) ? 1.0 : 0.35;
            float g = g_ChargerGlow;
            o.Color.rgb *= lerp(float3(1, 1, 1), float3(g, g * 0.25, g * 0.2), on);
        }
        else if (ph == SWARM_CHARGE_DASH)
            o.Color.rgb *= float3(1.8, 0.6, 0.5);
    }

    // ---- sand mage: sand-yellow glow swelling through the wind-up, a last flash right before the tornado leaves (2026-10-17) ----
    if (extra.kind == SWARM_KIND_SANDMAGE && extra.fuse > 0.0)
    {
        float u = saturate(extra.fuse / max(g_SandMageWindup, 1e-3));
        float pulse = 0.5 + 0.5 * sin(extra.fuse * (12.0 + 30.0 * u));
        float g = 1.0 + (0.6 + 1.6 * u) * pulse;
        o.Color.rgb *= float3(g, g * 0.85, 1.0 + (g - 1.0) * 0.3);
    }

    // ---- ruins enemies (2026-10-25) ----
    // sleeping gargoyle: a dull grey statue (the glowing eyes go out), lights up as it wakes
    if (statue)
    {
        float l = dot(o.Color.rgb, float3(0.3, 0.59, 0.11));
        o.Color.rgb = min(lerp(o.Color.rgb, l.xxx, 0.8), 0.7);
    }
    // necromancer winding up: a sickly green glow swelling toward the cast
    if (extra.kind == SWARM_KIND_NECRO && extra.fuse > 0.0)
    {
        float u = saturate(extra.fuse / max(g_NecroWindup, 1e-3));
        float pulse = 0.5 + 0.5 * sin(extra.fuse * (8.0 + 22.0 * u));
        float g = 1.0 + (0.5 + 1.5 * u) * pulse;
        o.Color.rgb *= float3(1.0 + (g - 1.0) * 0.45, g, 1.0 + (g - 1.0) * 0.6);
    }
    // chain warden winding up the throw: an orange glow (with the band on the ground)
    if (extra.kind == SWARM_KIND_WARDEN && extra.fuse > 0.0)
    {
        float u = saturate(extra.fuse / max(g_WardenWindup, 1e-3));
        float g = 1.0 + 1.6 * u * (0.6 + 0.4 * sin(extra.fuse * 30.0));
        o.Color.rgb *= float3(g, 1.0 + (g - 1.0) * 0.45, 1.0 + (g - 1.0) * 0.1);
    }
    // the revived cursed armor: a slow violet pulse (it came back once)
    if (extra.kind == SWARM_KIND_ARMOR2)
        o.Color.rgb *= float3(1.15, 0.8, 1.45) * (0.9 + 0.25 * sin(g_AnimTime * 3.0 + (float) slot));
    // fixed elites (2026-10-25): enraged = a red pulse, winding up a move = a hot flicker that brightens toward the hit
    if ((eliteFlags & SWARM_ELITE_ENRAGED) != 0u)
        o.Color.rgb *= float3(1.55, 0.6, 0.5) * (0.9 + 0.2 * sin(g_AnimTime * 7.0 + (float) slot));
    if (eliteGlow > 0.0)
    {
        float g = 1.0 + eliteGlow * (1.3 + 0.7 * sin(g_AnimTime * 28.0));
        o.Color.rgb *= float3(g, 1.0 + (g - 1.0) * 0.55, 1.0 + (g - 1.0) * 0.35);
    }

    // ---- frozen (Ice Lance): pale blue body ----
    if (frozen)
        o.Color.rgb *= lerp(float3(1, 1, 1), kIceTint, saturate(g_FreezeTint));
    // ---- poisoned (toxin still draining): sickly green, stronger with more toxin stored ----
    float toxin = enemyPoison[slot].x;
    if (toxin > 0.0)
        o.Color.rgb *= lerp(float3(1, 1, 1), kPoisonTint, saturate(toxin * 0.5) * 0.85);
    return o;
}