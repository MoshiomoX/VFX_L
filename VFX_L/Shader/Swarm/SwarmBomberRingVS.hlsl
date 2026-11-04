// ============================================================
// SwarmBomberRingVS.hlsl
// 点火した自爆兵の足元に出す予告の円: 縁が g_BomberBlastRadius を示し、
// 円盤が導火線に合わせて中心から大きくなり、爆発の瞬間に
// 縁に届く。インスタンス 1 つに地面の四角 1 枚、
// 頂点 6 つを SV_VertexID から作る。頂点バッファなし。
//
// インスタンスは自爆兵のリスト（SwarmEnemyCompactCS）から来て、
// DrawInstancedIndirect で描く。点火していない（またはもう
// 死んだ）自爆兵はニアプレーンの後ろへ潰すので、導火線の最中に
// 倒された瞬間に円が消える。
//
// 四角は自爆兵の足の高さ（position.y - groundY
// = 地形の高さ、SwarmEnemyMoveCS を参照）+ g_RingLift に平らに置く。坂の上では
// 一部が斜面の下に沈むことがある。
//
// 突撃爆弾兵のリストからもう一度描く（2026-10-19）: 点火した突撃爆弾兵にも
// 同じ円を自分の半径（g_DashBomberBlastRadius）と
// 導火線（g_DashBomberWindup + g_DashBomberFuse）で出す。走るので円も足元で一緒に走る。
//
// cbuffer の C++ 側: SwarmSystem::BomberRingCB
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
    float4 g_RingFill;  // 大きくなる円盤（ストレートアルファ）
    float4 g_RingEdge;  // 縁
    float4 g_RingBack;  // 縁の内側でまだ塗られていない所
    float g_RingEdgeWidth; // 縁の太さ、メートル
    float g_RingLift;      // 地面からのメートル
    float2 _ringPad;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> bomberList : register(t2);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);

struct RingOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;       // 爆発半径の方向に -1..1
    float progress : TEXCOORD1;  // 導火線 0..1 = 塗られた円盤の半径（uv 単位）
    float edgeWidth : TEXCOORD2; // 縁の太さ（uv 単位）
    float2 shapeFill : TEXCOORD3; // ここでは常に 0: 丸い円（SwarmBomberRingPS。設定するのは予告の扇形 / 帯だけ）
};

static const float2 kCorner[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

RingOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    RingOut o = (RingOut) 0;

    uint slot = bomberList[iid];
    SwarmEnemyExtra extra = enemyExtra[slot];
    bool dash = (extra.kind == SWARM_KIND_DASHBOMBER);
    bool lit = (extra.kind == SWARM_KIND_BOMBER || dash) && (extra.fuse > 0.0);
    if (enemyStates[slot] == SWARM_DEAD || !lit)
    {
        o.pos = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    float radius = max(dash ? g_DashBomberBlastRadius : g_BomberBlastRadius, 0.01);
    float fuseTime = dash ? g_DashBomberWindup + g_DashBomberFuse : g_BomberFuseTime;
    float3 p = enemies[slot].position;
    float2 c = kCorner[vid];
    float3 world = float3(p.x + c.x * radius,
                          p.y - g_GroundY + g_RingLift,
                          p.z + c.y * radius);

    o.pos = mul(mul(float4(world, 1.0), g_RingView), g_RingProj);
    o.uv = c;
    o.progress = saturate(extra.fuse / max(fuseTime, 1e-3));
    o.edgeWidth = g_RingEdgeWidth / radius;
    return o;
}
