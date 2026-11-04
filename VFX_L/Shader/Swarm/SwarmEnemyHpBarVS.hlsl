// ============================================================
// SwarmEnemyHpBarVS.hlsl
// 生きている雑魚の頭上に浮かぶ HP バー。敵 1 体につきカメラを向く
// 四角形 1 枚、SV_VertexID から 6 頂点、頂点バッファなし。インスタンスは
// 雑魚のメッシュと同じ生存リスト（SwarmEnemyCompactCS）から取り、
// DrawInstancedIndirect で描く。
//
// サイズはワールド単位（メッシュと同じく遠いほど小さい）。
// 塗りの割合 = hp / 生成時の hp。生成時の hp は別のバッファ
//（enemyMaxHp、SwarmSpawnEnemyCS / SwarmRecycleCS が書く）にあるので
// SwarmEnemy は 48 バイトのまま。
//
// cbuffer の C++ 側の写し：SwarmSystem.cpp の SwarmHpBarCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b4   // エリートの体の大きさ：バーを高く、幅広くする
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmHpBarCB : register(b0)
{
    row_major float4x4 g_BarView;
    row_major float4x4 g_BarProj;
    float g_BarWidth;   // メートル
    float g_BarHeight;  // メートル
    float g_BarOffset;  // SwarmEnemy.position（カプセルの中心）から上へのメートル
    float g_BarBorder;  // メートル
    float4 g_BarFill;
    float4 g_BarBack;
    float4 g_BarEdge;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> aliveList : register(t2);
StructuredBuffer<uint> enemyMaxHp : register(t3);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t4);
#define SWARM_ICE_MAP_REG t5
#define SWARM_POND_TOP_REG t6
#include "../Common/SwarmIceLift.hlsli"

struct BarOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;   // (0,0) 左上 .. (1,1) 右下
    float ratio : TEXCOORD1; // 0..1 残り hp
};

static const float2 kCorner[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

BarOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    BarOut o = (BarOut) 0;

    uint slot = aliveList[iid];
    if (enemyStates[slot] == SWARM_DEAD || (SwarmUntargetable(enemyExtra[slot]) && enemyExtra[slot].kind != SWARM_KIND_BURROWER))   // 潜っている砂潜りはバーを残す：地面の範囲が当たるため（2026-10-26）
    {
        o.pos = float4(0, 0, -1, 1); // クリップ
        return o;
    }

    SwarmEnemy e = enemies[slot];

    // hp は負の値の InterlockedAdd で減るので、とどめの一撃で状態が
    // 変わる前に 0 を越えて回り込む（巨大な uint）ことがある：
    // 生成時の値を超えていれば空として扱う
    uint maxHp = max(enemyMaxHp[slot], 1u);
    o.ratio = (e.hp > maxHp) ? 0.0 : (float) e.hp / (float) maxHp;

    // ワールド空間でのカメラの軸 = ビュー行列の最初の 2 列
    //（行ベクトルの規約）。座標系の左右に関係なく、バーが画面を向き
    // 塗りが左から右へ伸びるようにする
    float3 right = float3(g_BarView._11, g_BarView._21, g_BarView._31);
    float3 up = float3(g_BarView._12, g_BarView._22, g_BarView._32);

    // エリートは足元を基点に拡大する：バーは頭と一緒に上がり、幅も広がる
    //（狂暴化した固定エリートはさらに大きく描く、SwarmSlotScale、2026-10-25）
    float k = SwarmSlotScale(slot, enemyExtra[slot].kind);
    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float offset = footY + (g_BarOffset - footY) * k;
    float width = g_BarWidth * (k > 1.0 ? 1.5 : 1.0);

    float2 c = kCorner[vid];
    float3 center = e.position + float3(0.0, offset, 0.0);
    if (g_IceOn > 0.5)
        center.y += IceLift(e.position.xz, e.position.y - g_GroundY);
    float3 world = center
        + right * ((c.x - 0.5) * width)
        + up * ((0.5 - c.y) * g_BarHeight);

    o.pos = mul(mul(float4(world, 1.0), g_BarView), g_BarProj);
    o.uv = c;
    return o;
}
