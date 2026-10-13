// ============================================================
// EmitInit.hlsli
// Shared by the GPU-side emitters that write particles straight into
// the pool (SwarmEmitCS: projectiles / areas / orbs, GrassFireEmitCS:
// burning grass). Pulled out of SwarmEmitCS on 2026-10-12 so the fire
// emitter does not copy 40 lines of particle init.
//
//   StochasticCount          emit count from a continuous rate, no drift
//   EmitShape                position / velocity from the emitter's shape
//   InitParticleFromEmitter  the particle itself (same as ParticleEmitCS)
//
// Includes ParticleCommon and the Emit*.hlsli shapes; mesh emit is not
// supported on this path (no source vertex buffer).
// ============================================================
#ifndef EMIT_INIT_HLSLI
#define EMIT_INIT_HLSLI

#include "ParticleCommon.hlsli"
#include "EmitPoint.hlsli"
#include "EmitSphere.hlsli"
#include "EmitCone.hlsli"
#include "EmitBox.hlsli"
#include "EmitRing.hlsli"
#include "EmitDisc.hlsli"

// emit count from a continuous rate without drift
uint StochasticCount(float rate, float dt, inout uint seed)
{
    float f = rate * dt;
    uint k = (uint) floor(f);
    if (Random(seed) < frac(f))
        k += 1u;
    return k;
}

// pos / vel start at the emitter's position / zero; the shape moves them
void EmitShape(GPUEmitter e, inout uint s, inout float3 pos, inout float3 vel)
{
    switch (e.emitType)
    {
        case 0:
            EmitPoint(e, s, pos, vel);
            break;
        case 1:
            EmitSphere(e, s, pos, vel);
            break;
        case 2:
            EmitCone(e, s, pos, vel);
            break;
        case 3:
            EmitBox(e, s, pos, vel);
            break;
        case 4:
            EmitRing(e, s, pos, vel);
            break;
        case 5:
            EmitDisc(e, s, pos, vel);
            break;
        default:
            break; // mesh emit not supported on this path
    }
}

// particle init: identical to ParticleEmitCS. ownerID 0 = unowned
GPUParticle InitParticleFromEmitter(GPUEmitter e, uint s, float3 pos, float3 vel)
{
    GPUParticle q = (GPUParticle) 0;
    q.position = pos;
    q.velocity = vel;
    q.acceleration = e.gravity;
    q.drag = e.dragCoeff;
    q.lifetime = RandomRange(s, e.lifetimeRange.x, e.lifetimeRange.y);
    q.age = 0.0;
    q.isAlive = 1.0;
    q.startColor = RandomColor(s, e.startColorMin, e.startColorMax);
    q.endColor = RandomColor(s, e.endColorMin, e.endColorMax);
    q.color = q.startColor;
    q.startSize = RandomRange(s, e.sizeRange.x, e.sizeRange.y);
    q.endSize = RandomRange(s, e.sizeRange.z, e.sizeRange.w);
    q.size = q.startSize;
    q.rotation = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
    q.angularVel = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);

    // cube: 3 axes share the same ranges (same as ParticleEmitCS)
    q.renderMode = e.renderMode;
    q.rot3.x = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
    q.rot3.y = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
    q.rot3.z = RandomRange(s, e.rotationRange.x, e.rotationRange.y);
    q.angVel3.x = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);
    q.angVel3.y = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);
    q.angVel3.z = RandomRange(s, e.angularVelRange.x, e.angularVelRange.y);
    q.seed = s;
    q.textureIndex = e.textureIndex;
    q.atlasRows = e.atlasRows;
    q.atlasCols = e.atlasCols;
    InitParticleFrame(e.frameMode, e.atlasIndex, e.frameCount,
                      e.atlasRows, e.atlasCols, s, q.uvFrame, q.atlasAnimate);
    q.colorKeyOffset = e.colorKeyoffset; // static region, offset 0-based
    q.colorKeyCount = e.colorKeyCount;
    q.ownerID = 0;
    return q;
}

#endif
