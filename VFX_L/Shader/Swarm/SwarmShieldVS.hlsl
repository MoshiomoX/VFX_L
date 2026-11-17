// ============================================================
// SwarmShieldVS.hlsl
// 盾兵が体の前に構える塔盾。
// 盾兵 1 体に 1 インスタンス（盾の描画一覧、
// 数は DrawIndexedInstancedIndirect）。メッシュはエフェクト用モデル
// （Assets/VFX/Mesh/dun01.FBX）で、敵のピクセルシェーダーと
// 既定の白テクスチャで描くので、色は g_PropTint になる。
//
// メッシュは中心（g_MeshAnchor）が敵の座標系（x 右、y 足元から上、
// z 前）の g_HoldOffset に来るように動かし、g_MeshScale で拡大して
// g_MeshYaw で回す。歩いている間は少し上下し、体と一緒に
// 大きくなり（SwarmKindScale、足元を基点）、体と同じ被弾の閃光 /
// 氷の色を受ける。
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_KIND_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> shieldList : register(t2);
StructuredBuffer<float4> enemyStatus : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

#include "SwarmPropMeshCB.hlsli"   // g_PropTint = 盾の色、g_PropParam = 歩く時の上下の揺れ

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

#include "SwarmEnemyLook.hlsli"   // kIceTint、RotateY

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = shieldList[input.InstanceID];
    SwarmEnemyExtra ex = enemyExtra[slot];
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_SHIELD)
    {
        o.Position = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmEnemy e = enemies[slot];
    bool frozen = enemyStatus[slot].z > 0.0;
    bool moving = dot(e.velocity.xz, e.velocity.xz) > 0.01 && !frozen && e.animIndex != SWARM_ANIM_STUN;

    float ms, mc;
    sincos(g_MeshYaw, ms, mc);
    float3 local = RotateY((input.Position - g_MeshAnchor) * g_MeshScale, ms, mc);
    float3 normal = RotateY(input.Normal, ms, mc);
    float3 tangent = RotateY(input.Tangent, ms, mc);

    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float bob = moving ? abs(sin(e.animTime * 9.0 + (float) slot * 0.37)) * g_PropParam : 0.0;
    local += g_HoldOffset;
    local.y += footY + bob;

    // 足元を基点に体と一緒に大きくなる（SwarmEnemyVS の体と同じ）
    float k = SwarmKindScale(ex.kind);
    local.xz *= k;
    local.y = footY + (local.y - footY) * k;

    float s, c;
    sincos(e.yaw, s, c);
    float3 worldPos = RotateY(local, s, c) + e.position;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = RotateY(normal, s, c);
    o.Tangent = RotateY(tangent, s, c);
    o.UV = input.UV;
    o.Color = g_PropTint;
    if (e.animIndex == SWARM_ANIM_STUN)
    {
        float t = saturate(e.animTime / max(g_HitStun, 1e-4));
        o.Color.rgb *= lerp(g_HitFlash, 1.0, t);
    }
    if (frozen)
        o.Color.rgb *= lerp(float3(1, 1, 1), kIceTint, saturate(g_FreezeTint));
    return o;
}
