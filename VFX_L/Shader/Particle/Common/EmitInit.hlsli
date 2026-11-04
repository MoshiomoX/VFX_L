// ============================================================
// EmitInit.hlsli
// パーティクルをプールへ直接書く GPU 側のエミッタで共有する
// （SwarmEmitCS: 弾 / 範囲 / 経験玉、GrassFireEmitCS:
// 燃える草）。2026-10-12 に SwarmEmitCS から切り出した。火の
// エミッタがパーティクル初期化の 40 行をコピーしないため。
//
//   StochasticCount          連続的な発生率から放出数を出す。ずれが溜まらない
//   EmitShape                エミッタの形状から位置 / 速度を出す
//   InitParticleFromEmitter  パーティクルそのもの（ParticleEmitCS と同じ）
//
// ParticleCommon と Emit*.hlsli の形状を include する。このパスでは
// メッシュからの放出は使えない（元の頂点バッファが無い）。
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

// 連続的な発生率から、ずれを溜めずに放出数を出す
uint StochasticCount(float rate, float dt, inout uint seed)
{
    float f = rate * dt;
    uint k = (uint) floor(f);
    if (Random(seed) < frac(f))
        k += 1u;
    return k;
}

// pos / vel はエミッタの位置 / 0 から始まり、形状がそれを動かす
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
            break; // このパスではメッシュからの放出は未対応
    }
}

// パーティクルの初期化: ParticleEmitCS と同じ。ownerID 0 = 持ち主なし
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

    // 立方体: 3 軸とも同じ範囲を使う（ParticleEmitCS と同じ）
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
    q.colorKeyOffset = e.colorKeyoffset; // 静的な領域、オフセットは 0 始まり
    q.colorKeyCount = e.colorKeyCount;
    q.ownerID = 0;
    return q;
}

#endif
