// ============================================================
// SwarmEnemyLook.hlsli
// 敵の見た目の VS（SwarmEnemyVS / CorpseVS / ShieldVS / IceVS / SpikeBallVS）で共用する
// 色と、yaw での回転。
// ============================================================
#ifndef SWARM_ENEMY_LOOK_HLSLI
#define SWARM_ENEMY_LOOK_HLSLI

// 凍結：淡い青、g_FreezeTint を掛ける
static const float3 kIceTint = float3(0.75, 1.05, 1.7);
// エリート：共通のゾンビテクスチャに暗い赤を乗せる
static const float3 kEliteTint = float3(1.4, 0.55, 0.5);
// 幽霊：半透明の青白、アルファブレンドで描く（SwarmSystem がこの一覧を最後に描く）
static const float3 kGhostTint = float3(0.55, 1.25, 1.6);

// Y 軸まわりの回転（s, c = sin / cos yaw）。yaw = atan2(v.x, v.z)：前方 (0,0,1) -> (sin, 0, cos)
float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

#endif
