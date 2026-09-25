// ============================================================
// EffectTrail.hlsli
// Ribbon trails driven by an anchor point (VFX Trail entry: the
// effect position + offset), fully on the GPU. The per-particle
// trails (ParticleTrail.hlsli) follow each particle's own motion;
// these follow the effect, so a still particle core still leaves one.
//
// Every trail instance owns a ring of EFFECT_TRAIL_POINTS committed
// points and one state record:
//     effectTrailPoints[trailIndex * EFFECT_TRAIL_POINTS + slot]
//     effectTrailStates[trailIndex]
//
// Each frame one anchor per instance comes in (EffectTrailCS):
//   - the live head follows the anchor. headTime = the last time it
//     actually moved, so a head that stops ages like any other point
//   - a point is committed once the head is minDistance away from the
//     newest point (distance based, like Unity's TrailRenderer)
//   - every point lives <style.lifetime> seconds. The tail is cut at
//     exactly age == lifetime, so the ribbon shrinks smoothly
//   - command NONE = detached: no new points, it shrinks and vanishes
//
// Strip layout (EffectTrailVS), one instance = one trail:
//   pair 0                : live head
//   pair 1 .. validCount  : ring points, newest first
//   pair validCount + 1   : tail (cut point, or the oldest point)
//   pairs past the tail collapse onto it (degenerate triangles)
//
// Must match EffectTrail*GPU / kEffectTrailPoints in GPUParticle.h
// ============================================================
#ifndef EFFECT_TRAIL_HLSLI
#define EFFECT_TRAIL_HLSLI

#define EFFECT_TRAIL_POINTS 64u
#define EFFECT_TRAIL_STRIP_PAIRS (EFFECT_TRAIL_POINTS + 2u)

#define EFFECT_TRAIL_CMD_NONE  0u // detached: keep fading, no new points
#define EFFECT_TRAIL_CMD_MOVE  1u // the head is at position this frame
#define EFFECT_TRAIL_CMD_RESET 2u // new trail starting at position

// written every frame for every instance (CPU upload). 32 bytes
struct EffectTrailAnchor
{
    float3 position;
    float minDistance;
    uint styleSlot; // style index + 1, 0 = unused instance
    uint command;
    uint2 _pad;
};

// 20 bytes
struct EffectTrailPoint
{
    float3 position;
    float time; // clock value when committed
    float distance; // path length from the trail start (Tile UV)
};

// GPU only. 64 bytes
struct EffectTrailState
{
    float3 headPos;
    float headTime;
    float headDist;
    uint ringHead; // newest committed point
    uint ringCount; // committed points kept in the ring
    uint validCount; // ring points in front of the tail (this frame)
    float3 tailPos;
    float tailTime;
    float tailDist;
    float3 _pad;
};

uint EffectTrailSlot(uint ringHead, uint newestFirst)
{
    return (ringHead + EFFECT_TRAIL_POINTS - newestFirst) % EFFECT_TRAIL_POINTS;
}

#endif
