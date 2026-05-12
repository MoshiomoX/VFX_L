// ============================================
// EmitRing.hlsli
// リング発射 — 環状の帯から発射
// shapeSize.x = 外径
// shapeSize.y = 内径
// direction = リング法線
// ============================================

#ifndef EMIT_RING_HLSLI
#define EMIT_RING_HLSLI

void EmitRing(GPUEmitter e, inout uint seed, out float3 pos, out float3 vel)
{
    float outerRadius = e.shapeSize.x;
    float innerRadius = e.shapeSize.y;

    // 内径～外径間のランダム半径（均一分布補正）
    float r = sqrt(lerp(innerRadius * innerRadius, outerRadius * outerRadius, Random(seed)));
    float angle = Random(seed) * 6.28318530718;

    // リングの平面座標系を構築
    float3 normal = normalize(e.direction);
    float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, normal));
    up = cross(normal, right);

    float3 offset = (right * cos(angle) + up * sin(angle)) * r;
    pos = e.position + offset;

    // 速度は外向き（リング平面上）
    float3 dir = normalize(offset);
    float speed = RandomRange(seed, e.speedRange.x, e.speedRange.y);
    vel = dir * speed;
}

#endif