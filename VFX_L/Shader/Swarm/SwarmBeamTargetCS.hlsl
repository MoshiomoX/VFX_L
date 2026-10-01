// ============================================================
// SwarmBeamTargetCS.hlsl
// Target of each beam (2026-10-01). One thread group per beam channel.
//
// The CPU (WeaponSystem) turns a beam toward the position read back
// from here, at a bounded rate. Per channel, from BeamTargetCB:
//   cmd 0 = idle (channel unused, nothing written)
//   cmd 1 = start: lock the enemy nearest to the seek point (where the
//           triggering projectile died), within kLockRadius
//   cmd 2 = track: keep the locked enemy while it is alive and within
//           reach; otherwise pick the alive enemy within the beam length
//           whose direction is closest to the beam (smallest turn)
// The result (slot, serial, valid, position) stays in beamTargets, which
// is also the state for the next frame (the locked slot). serial tells
// the CPU which beam the answer belongs to (a channel is reused).
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer BeamTargetCB : register(b4)
{
    float4 g_BTOrigin[SWARM_MAX_BEAMS]; // xyz = muzzle, w = asfloat(cmd)
    float4 g_BTDir[SWARM_MAX_BEAMS];    // xyz = current beam direction, w = beam length
    float4 g_BTSeek[SWARM_MAX_BEAMS];   // xyz = seek point (cmd 1), w = asfloat(serial)
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
// per channel 32 bytes: slot, serial, valid, pad, pos.xyz, pad
RWByteAddressBuffer beamTargets : register(u0);

static const uint kNoSlot = 0xFFFFFFFFu;
static const float kLockRadius = 2.5;  // start: the enemy the projectile hit is within this of where it died
static const float kKeepReach = 1.15;  // a locked enemy farther than length x this is dropped (recycled far away)

groupshared uint gBest;

[numthreads(256, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    uint ch = gid.x;
    uint cmd = asuint(g_BTOrigin[ch].w);
    if (cmd == 0u)
        return; // uniform for the whole group (constant buffer)

    uint base = ch * 32u;
    uint serial = asuint(g_BTSeek[ch].w);
    float3 origin = g_BTOrigin[ch].xyz;
    float3 dir = g_BTDir[ch].xyz;
    float len = g_BTDir[ch].w;
    float3 seek = g_BTSeek[ch].xyz;

    // keep the locked enemy?
    uint curSlot = beamTargets.Load(base + 0u);
    uint curSerial = beamTargets.Load(base + 4u);
    bool keep = false;
    if (cmd == 2u && curSerial == serial && curSlot < g_MaxEnemies && enemyStates[curSlot] != SWARM_DEAD)
    {
        float2 d = enemies[curSlot].position.xz - origin.xz;
        keep = dot(d, d) <= (len * kKeepReach) * (len * kKeepReach);
    }

    if (gi == 0u)
        gBest = kNoSlot;
    GroupMemoryBarrierWithGroupSync();

    if (!keep)
    {
        float2 fwd = dir.xz;
        float fl = length(fwd);
        fwd = (fl > 1e-4) ? fwd / fl : float2(0.0, 1.0);
        for (uint i = gi; i < g_MaxEnemies; i += 256u)
        {
            if (enemyStates[i] == SWARM_DEAD)
                continue;
            float3 p = enemies[i].position;
            float score;
            if (cmd == 1u)
            {
                // start: nearest to the seek point
                score = length(p.xz - seek.xz);
                if (score > kLockRadius)
                    continue;
            }
            else
            {
                // retarget: smallest angle from the beam, within its length
                float2 to = p.xz - origin.xz;
                float l = length(to);
                if (l > len || l < 1e-3)
                    continue;
                score = max(1.0 - dot(to / l, fwd), 0.0); // 0 = dead ahead, 2 = behind
            }
            uint prev;
            InterlockedMin(gBest, (asuint(score) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK), prev);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (gi == 0u)
    {
        uint slot = keep ? curSlot : ((gBest == kNoSlot) ? kNoSlot : (gBest & SWARM_SLOT_MASK));
        bool valid = (slot != kNoSlot);
        float3 pos = valid ? enemies[slot].position : float3(0, 0, 0);
        beamTargets.Store4(base, uint4(slot, serial, valid ? 1u : 0u, 0u));
        beamTargets.Store4(base + 16u, uint4(asuint(pos.x), asuint(pos.y), asuint(pos.z), 0u));
    }
}
