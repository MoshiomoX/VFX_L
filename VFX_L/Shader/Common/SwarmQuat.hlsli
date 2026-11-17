// ============================================================
// SwarmQuat.hlsli
// クォータニオンの補助関数（トゲ玉の転がり）。
// ============================================================

#ifndef SWARM_QUAT_HLSLI
#define SWARM_QUAT_HLSLI

// ============================================================
// クォータニオン（xyz = axis * sin(a / 2)、w = cos(a / 2)）。トゲ
// 玉の転がり：MoveCS がこのステップの回転をスロットの
// 向き（enemyRoll）に重ね、SwarmSpikeBallVS がそれでメッシュを回す
// ============================================================
float4 SwarmQuatAxisAngle(float3 axis, float angle)
{
    float s, c;
    sincos(0.5 * angle, s, c);
    return float4(axis * s, c);
}
// a * b = 先に b、次に a
float4 SwarmQuatMul(float4 a, float4 b)
{
    return float4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}
float3 SwarmQuatRotate(float4 q, float3 v)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}
// トゲ玉の保存済みの向き。まだ一度も書かれていない（ゼロ）= スロットごとに固定の姿勢
float4 SwarmSpikeBallRoll(float4 stored, uint slot)
{
    return (dot(stored, stored) > 0.25) ? stored
         : SwarmQuatAxisAngle(normalize(float3(0.3, 1.0, 0.2)), (float) slot * 1.7);
}

#endif
