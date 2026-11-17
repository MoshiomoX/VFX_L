// ============================================================
// SwarmSpikeBallVS.hlsl
// トゲ玉（SWARM_KIND_SPIKEBALL）:箱型の体ではなく、
// 転がるトゲの玉になっている砂漠の敵。トゲ玉 1 つに 1 インスタンス
// （トゲ玉の描画リスト、数は DrawIndexedInstancedIndirect）。メッシュは
// コードで作り
// （PrimitiveBuilder::CreateSpikeBall、半径 1、色は頂点に入っている）、
// 敵のピクセルシェーダーと既定の白テクスチャで描くので、
// 色は頂点カラー x g_PropTint になる。
//
// 玉は地面に乗り（中心 = 足元 + 半径）、種類の体格（SwarmKindScale）で
// 拡大し、enemyRoll の向きで回す: SwarmEnemyMoveCS が
// 毎ステップの転がり（進んだ距離 / 半径 を 上 x 進行方向 の軸まわりに）を
// 積み重ねていくので、加速・減速・方向転換で姿勢が飛ばない。一度も転がっていない玉は
// スロットごとに決まった姿勢のまま。被弾の光と氷の色は体と同じ。
// 凍ると転がるのをやめる。
//
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_KIND_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> spikeBallList : register(t2);
StructuredBuffer<float4> enemyRoll : register(t3);   // スロットごとの向き（クォータニオン）、SwarmEnemyMoveCS
StructuredBuffer<float4> enemyStatus : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// 転がる速さはここではなく KindCB の g_SpikeBallRollPerMeter として MoveCS へ
#include "SwarmPropMeshCB.hlsli"   // g_MeshScale = 体の半径、g_HoldOffset.y = 中心の高さ

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

#include "SwarmEnemyLook.hlsli"   // kIceTint

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = spikeBallList[input.InstanceID];
    SwarmEnemyExtra ex = enemyExtra[slot];
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_SPIKEBALL)
    {
        o.Position = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmEnemy e = enemies[slot];
    bool frozen = enemyStatus[slot].z > 0.0;
    float k = SwarmKindScale(ex.kind);
    float radius = g_MeshScale * k;

    // 転がり: MoveCS が実際に進んだ距離から積み上げた向き（冒頭の説明参照）
    float4 roll = SwarmSpikeBallRoll(enemyRoll[slot], slot);
    float3 local = SwarmQuatRotate(roll, input.Position * radius);
    float3 normal = SwarmQuatRotate(roll, input.Normal);

    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float3 worldPos = e.position + float3(0.0, footY + g_HoldOffset.y * k, 0.0) + local;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normal;
    o.Tangent = float3(1, 0, 0);
    o.UV = input.UV;
    o.Color = input.Color * g_PropTint;
    if (e.animIndex == SWARM_ANIM_STUN)
    {
        float t = saturate(e.animTime / max(g_HitStun, 1e-4));
        o.Color.rgb *= lerp(g_HitFlash, 1.0, t);
    }
    if (frozen)
        o.Color.rgb *= lerp(float3(1, 1, 1), kIceTint, saturate(g_FreezeTint));
    return o;
}
