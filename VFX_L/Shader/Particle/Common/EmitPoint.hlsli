// ============================================
// EmitPoint.hlsli
// 点発射 — 指定位置から拡散角内にランダム発射
// ============================================

#ifndef EMIT_POINT_HLSLI
#define EMIT_POINT_HLSLI

void EmitPoint(GPUEmitter e, inout uint seed, out float3 pos, out float3 vel)
{
    // 発射位置 = Emitterの位置そのまま
    pos = e.position;

    // 拡散角があればコーン内、なければ直進
    float3 dir;
    if (e.spreadAngle > 0.0)
    {
        dir = RandomInCone(seed, normalize(e.direction), e.spreadAngle);
    }
    else
    {
        dir = normalize(e.direction);
    }

    float speed = RandomRange(seed, e.speedRange.x, e.speedRange.y);
    vel = dir * speed;
}

#endif