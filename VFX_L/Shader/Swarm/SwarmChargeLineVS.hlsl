// ============================================================
// SwarmChargeLineVS.hlsl
// 溜め中の突撃兵の前の地面に出す赤い帯
// （2026-10-08）: 突進がどこを通るかをそのまま見せるので、プレイヤーは
// 横によけられる。突撃兵 1 体に 1 インスタンス（突撃兵の描画リスト、
// 数はリストから DrawInstancedIndirect で取る）。溜めていない
// 突撃兵は潰す。
//
// 帯は突撃兵の足元から始まり、yaw（溜めを始めたときに固定、
// SwarmEnemyAICS を参照）に沿って突進距離 + 体の半径だけ伸びる。
// それに沿って LINE_SEGS 枚の四角、どの頂点も高さ場に乗る。
// 塗りは溜めに合わせて突撃兵から先端へ伸び、先端に
// 届いたら突撃兵が走り出す。
//
// ピクセルシェーダ: SwarmChargeLinePS（b0 は自爆兵の円と同じ:
// SwarmSystem::BomberRingCB、色は SwarmSystem::chargeLine）。
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b4
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmBomberRingCB : register(b0)
{
    row_major float4x4 g_RingView;
    row_major float4x4 g_RingProj;
    float4 g_RingFill;
    float4 g_RingEdge;
    float4 g_RingBack;
    float g_RingEdgeWidth; // 横の縁、メートル
    float g_RingLift;      // 地面からのメートル
    float2 _ringPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> chargerList : register(t2);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);
StructuredBuffer<float> heights : register(t4);

static const uint LINE_SEGS = 24u;
// 帯の半幅（体の半径の倍数。体より少し広い: 当たるのはその範囲）
static const float LINE_WIDTH_MUL = 1.4;

struct LineOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;      // x = 突撃兵で 0 .. 先端で 1、y = 横方向に -1 .. 1
    float progress : TEXCOORD1; // この x まで塗る
    float2 size : TEXCOORD2;    // x = 長さ m、y = 横の縁（uv 単位）
};

static const float2 kCorner[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

LineOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    LineOut o = (LineOut) 0;
    // 鎖の番人の投擲と石像ガーゴイルの急降下にも同じ帯（2026-10-25）: C++ 側がリスト（突撃兵、番人、
    // ガーゴイル）ごとに 1 回、そのリストを chargerList としてバインドして描く
    uint slot = chargerList[iid];
    SwarmEnemyExtra ex = enemyExtra[slot];
    float r = g_EnemyRadius * SwarmKindScale(ex.kind);
    float len = 0.0, halfW = r * LINE_WIDTH_MUL, progress = -1.0;
    if (ex.kind == SWARM_KIND_CHARGER && SwarmChargerPhase(ex.fuse) == SWARM_CHARGE_WINDUP)
    {
        len = g_ChargerDashDist + r;
        progress = ex.fuse / max(g_ChargerWindup, 1e-3);
    }
    else if (ex.kind == SWARM_KIND_WARDEN && ex.fuse > 0.0)
    {
        len = g_WardenRange;
        halfW = 0.55;   // 鉤（CPU: 当たり半径 0.55 + プレイヤーの半径）
        progress = ex.fuse / max(g_WardenWindup, 1e-3);
    }
    else if (ex.kind == SWARM_KIND_GARGOYLE && ex.fuse >= SWARM_GARGOYLE_DIVE
             && SwarmGargoyleDiveT(ex) < g_GargoyleDiveWindup)
    {
        len = g_GargoyleDiveSpeed * g_GargoyleDiveTime + r;
        progress = SwarmGargoyleDiveT(ex) / max(g_GargoyleDiveWindup, 1e-3);
    }
    if (enemyStates[slot] == SWARM_DEAD || progress < 0.0)
    {
        o.pos = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float ys, yc;
    sincos(e.yaw, ys, yc);
    float2 dir = float2(ys, yc);
    float2 side = float2(dir.y, -dir.x);

    uint quad = vid / 6u;
    float2 c = kCorner[vid % 6u];
    float s = ((float) quad + c.x) / (float) LINE_SEGS;
    float across = c.y * 2.0 - 1.0;
    float2 xz = e.position.xz + dir * (s * len) + side * (across * halfW);
    float h = SwarmTerrainHeight(heights, xz);

    o.pos = mul(mul(float4(xz.x, h + g_RingLift, xz.y, 1.0), g_RingView), g_RingProj);
    o.uv = float2(s, across);
    o.progress = saturate(progress);
    o.size = float2(len, g_RingEdgeWidth / max(halfW, 1e-3));
    return o;
}
