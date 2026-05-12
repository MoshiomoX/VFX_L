// ============================================
// EmitSphere.hlsli
// 球面発射 — 球の表面または内部からランダム発射
// shapeSize.x = 半径
// ============================================

#ifndef EMIT_SPHERE_HLSLI
#define EMIT_SPHERE_HLSLI

void EmitSphere(GPUEmitter e, inout uint seed, out float3 pos, out float3 vel)
{
    float radius = e.shapeSize.x;

    // 球面上のランダムな点
    float3 offset = RandomOnSphere(seed) * radius;
    pos = e.position + offset;

    // 速度は中心から外向き
    float3 dir = normalize(offset);
    float speed = RandomRange(seed, e.speedRange.x, e.speedRange.y);
    vel = dir * speed;
}

#endif