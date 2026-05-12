// ============================================
// EmitDisc.hlsli
// ディスク発射 — 円盤の内部からランダム発射
// shapeSize.x = 半径
// direction = ディスク法線
// ============================================

#ifndef EMIT_DISC_HLSLI
#define EMIT_DISC_HLSLI

void EmitDisc(GPUEmitter e, inout uint seed, out float3 pos, out float3 vel)
{
    float radius = e.shapeSize.x;

    // 円盤内のランダム位置（均一分布補正）
    float r = sqrt(Random(seed)) * radius;
    float angle = Random(seed) * 6.28318530718;

    // ディスクの平面座標系を構築
    float3 normal = normalize(e.direction);
    float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, normal));
    up = cross(normal, right);

    float3 offset = (right * cos(angle) + up * sin(angle)) * r;
    pos = e.position + offset;

    // 速度は法線方向（拡散角あり）
    float3 dir;
    if (e.spreadAngle > 0.0)
    {
        dir = RandomInCone(seed, normal, e.spreadAngle);
    }
    else
    {
        dir = normal;
    }

    float speed = RandomRange(seed, e.speedRange.x, e.speedRange.y);
    vel = dir * speed;
}

#endif