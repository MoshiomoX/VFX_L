// ============================================================
// SwarmIceVS.hlsl
// 凍った敵のまわりの氷の結晶（アイスランス、2026-10-08）。
// 生きている敵 1 体に 1 インスタンス（SwarmEnemyCompactCS の生存リスト、
// DrawIndexedInstancedIndirect。2026-10-25 までは毎フレーム、プール全体
// x 2 だった）、2 回描く: g_IceVariant = 2 つの塊のどちらか。
// 凍っていない敵（enemySlow.z <= 0）は潰す。
// メッシュはエフェクトのモデル（Assets/VFX/Mesh/bingci_02.FBX、立った
// トゲの塊）で、敵のピクセルシェーダとデフォルトの白テクスチャで描き、
// アルファブレンドする（乗算済み: 光って
// 透けて見える氷）。
//
// 配置: メッシュの底の中心（g_MeshAnchor）を足元に置き、
// 塊を前に 1 つ、後ろに 1 つ、スロットごとのハッシュで回す。凍結の
// 最初の ICE_GROW 秒で生え上がり（enemySlow.w = 凍結の
// 長さなので w - z = 凍っている時間）、最後の
// ICE_MELT 秒で縮む。
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> aliveList : register(t2);   // 生きているスロット（SwarmEnemyCompactCS）
StructuredBuffer<float4> enemySlow : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// C++ 側: SwarmSystem::PropMeshCB（SwarmShieldVS と同じレイアウト）
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor; // モデル空間での塊の底の中心
    float g_MeshScale;   // モデル単位 -> メートル（普通の体格の塊）
    float3 g_HoldOffset; // x = 体の中心から各塊までの距離、y = 足元から沈める量
    float g_MeshYaw;     // 未使用
    float4 g_IceColor;   // リニア、1 を超えてもよい（ブルーム）
    float g_IceVariant;  // 0 = 前の塊、1 = 後ろの塊（それぞれ 1 回描く。2026-10-25、以前は未使用の bob）
    float g_IceAlpha;
    float2 _propPad;
};

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

static const float ICE_GROW = 0.12;
static const float ICE_MELT = 0.25;

float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = aliveList[input.InstanceID];
    uint variant = (g_IceVariant > 0.5) ? 1u : 0u;
    float4 st = (slot < g_MaxEnemies) ? enemySlow[slot] : float4(0, 0, 0, 0);
    if (slot >= g_MaxEnemies || enemyStates[slot] == SWARM_DEAD || st.z <= 0.0)
    {
        o.Position = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float k = SwarmKindScale(enemyExtra[slot].kind);
    float elapsed = max(st.w - st.z, 0.0);
    float size = k * smoothstep(0.0, ICE_GROW, elapsed) * lerp(0.5, 1.0, saturate(st.z / ICE_MELT));
    size *= (variant == 0u) ? 1.0 : 0.8;

    // スロットごとに回し、2 つ目の塊は反対側
    float a = frac((float) slot * 0.6180339) * 6.2831853 + (float) variant * 3.1415927;
    float as, ac;
    sincos(a, as, ac);
    float3 local = RotateY((input.Position - g_MeshAnchor) * g_MeshScale * size, as, ac);
    float3 normal = RotateY(input.Normal, as, ac);

    float footY = e.position.y - (g_EnemyRadius + g_EnemyCapsuleHalf);
    float3 worldPos = local + float3(e.position.x + as * g_HoldOffset.x * k,
                                     footY - g_HoldOffset.y,
                                     e.position.z + ac * g_HoldOffset.x * k);
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normal;
    o.Tangent = RotateY(input.Tangent, as, ac);
    o.UV = input.UV;
    o.Color = float4(g_IceColor.rgb, g_IceAlpha);
    return o;
}
