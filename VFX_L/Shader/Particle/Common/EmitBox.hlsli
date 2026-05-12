// ============================================
// EmitBox.hlsli
// ボックス発射 — 箱の内部からランダム発射
// shapeSize = xyz各軸の半径
// ============================================

#ifndef EMIT_BOX_HLSLI
#define EMIT_BOX_HLSLI

void EmitBox(GPUEmitter e, inout uint seed, out float3 pos, out float3 vel)
{
    // ボックス内のランダム位置
    float x = RandomRange(seed, -e.shapeSize.x, e.shapeSize.x);
    float y = RandomRange(seed, -e.shapeSize.y, e.shapeSize.y);
    float z = RandomRange(seed, -e.shapeSize.z, e.shapeSize.z);
    pos = e.position + float3(x, y, z);

    // 速度は発射方向（拡散角あり）
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