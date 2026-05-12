// ============================================
// EmitCone.hlsli
// 錐体発射 — 指定方向を軸に円錐状に発射
// shapeSize.x = 底面半径
// spreadAngle = 錐角
// ============================================

#ifndef EMIT_CONE_HLSLI
#define EMIT_CONE_HLSLI

void EmitCone(GPUEmitter e, inout uint seed, out float3 pos, out float3 vel)
{
    // 底面上のランダムオフセット（底面半径内）
    float r = sqrt(Random(seed)) * e.shapeSize.x;
    float angle = Random(seed) * 6.28318530718;

    // 底面のローカル座標系を構築
    float3 dir = normalize(e.direction);
    float3 up = abs(dir.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, dir));
    up = cross(dir, right);

    float3 offset = (right * cos(angle) + up * sin(angle)) * r;
    pos = e.position + offset;

    // 速度はコーン内のランダム方向
    float3 velDir = RandomInCone(seed, dir, e.spreadAngle);
    float speed = RandomRange(seed, e.speedRange.x, e.speedRange.y);
    vel = velDir * speed;
}

#endif