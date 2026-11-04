// ============================================================
// SwarmBlobShadowVS.hlsl
// 生きている雑魚それぞれの足元の地面に落とす丸い影。雑魚は太陽の
// シャドウマップ（ShadowMap）に入らないので、その代わり。インスタンス 1 つに
// 地面の四角形 1 枚、頂点 6 つは SV_VertexID から作り、頂点バッファは無い。
//
// インスタンスは雑魚のメッシュや HP バーと同じ生存一覧
// （SwarmEnemyCompactCS）から取り、DrawInstancedIndirect で描く（HP バーの
// 引数を使う: 頂点 6 つ x 生存数）。
//
// 各角はそれぞれの xz で地形の高さ場に乗る（四角形が高台の長い坂に
// 突き刺さらず沿う）。
// 足の高さから g_BlobClamp 以内に収めるので、
// 崖の縁にいる雑魚の四角形が崖下まで引き伸ばされない。
//
// C++ 側の cbuffer: SwarmSystem::BlobShadowCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b4   // g_IceOn とマップ（凍った池）
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmBlobShadowCB : register(b0)
{
    row_major float4x4 g_BlobView;
    row_major float4x4 g_BlobProj;
    float g_BlobRadius;    // m
    float g_BlobStrength;  // 中心の暗さ、0..1
    float g_BlobLift;      // 地面から浮かせる m（Z ファイティング対策）
    float g_BlobSoftness;  // 0..1、半径のうちぼかす割合
    float g_BlobClamp;     // 足からの角の高さの差の上限、m
    float3 _blobPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> aliveList : register(t2);
StructuredBuffer<float> heights : register(t3);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t6);
#define SWARM_ICE_MAP_REG t4
#define SWARM_POND_TOP_REG t5
#include "../Common/SwarmIceLift.hlsli"

struct BlobOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0; // 影の端から端まで -1..1
};

static const float2 kCorner[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

BlobOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    BlobOut o = (BlobOut) 0;
    o.pos = float4(0, 0, -1, 1); // 生きていなければクリップされる

    uint slot = aliveList[iid];
    if (enemyStates[slot] != SWARM_DEAD)
    {
        float3 p = enemies[slot].position;
        float footY = p.y - g_GroundY - SwarmBodyHover(enemyExtra[slot]);   // 飛んでいる敵の影も地面に落とす
        float2 c = kCorner[vid];
        float2 xz = p.xz + c * g_BlobRadius;
        float y = clamp(SwarmTerrainHeight(heights, xz), footY - g_BlobClamp, footY + g_BlobClamp);
        if (g_IceOn > 0.5)
            y += IceLift(p.xz, footY);
        float3 world = float3(xz.x, y + g_BlobLift, xz.y);

        o.pos = mul(mul(float4(world, 1.0), g_BlobView), g_BlobProj);
        o.uv = c;
    }
    return o;
}
