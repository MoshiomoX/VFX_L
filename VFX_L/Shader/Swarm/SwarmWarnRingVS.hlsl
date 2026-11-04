// ============================================================
// SwarmWarnRingVS.hlsl
// CPU が地面に置く予告の円（2026-10-03: ボスの
// 重撃、SwarmSystem::SetWarnCircles）。見た目は自爆兵 /
// メテオの円と同じ: 縁が爆発の半径を示し、円盤が中心から
// progress に合わせて大きくなり、発動の瞬間に縁に届く。
// ピクセルシェーダは SwarmBomberRingPS（b0 のレイアウトも同じ）、色は
// SwarmSystem::warnRing。
//
// 円 1 つに 1 インスタンス。メテオの円（平らな四角 1 枚）と違い、各円は
// WARN_GRID x WARN_GRID のグリッドで頂点が高さ場に乗るので、
// 坂の上や段差を跨ぐ円も地面に沿う。高さは
// 中心の +-WARN_MAX_STEP にクランプし、崖にかかった円が
// 崖の面を登らないようにする。頂点バッファなし: SV_VertexID。
//
// cbuffer の C++ 側: SwarmSystem::BomberRingCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
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

// SwarmSystem::WarnCircle（32 バイト）
struct SwarmWarnCircle
{
    float3 center;   // 地面の上
    float radius;
    float progress;  // 0..1。負 = 縁だけ（内側は塗らない）
    float band;      // > 0: 代わりに半径の所へこの幅（メートル）の帯を描く（ボスの衝撃波、2026-10-07）
    float yaw;       // 扇形 / 帯の向き（ラジアン、0 = +Z）（2026-10-25、固定エリート）
    float shape;     // > 0: この半角（ラジアン）で半径 `radius` の扇形。< 0: 幅 -shape m、長さ `radius` の帯
};

StructuredBuffer<SwarmWarnCircle> circles : register(t0);
StructuredBuffer<float> heights : register(t1);

static const uint WARN_GRID = 8u;
static const float WARN_MAX_STEP = 0.6;

struct RingOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;       // 半径方向に -1..1
    float progress : TEXCOORD1;  // 塗られた円盤の半径（uv 単位）
    float edgeWidth : TEXCOORD2; // 縁の太さ（uv 単位）
    float2 shapeFill : TEXCOORD3; // x = 1: 扇形 / 帯、y = その中でどこまで進んだか（0 根元 .. 1 先端）= 塗りが伸びていく量
};

static const float2 kCorner[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

RingOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    RingOut o = (RingOut) 0;

    uint quad = vid / 6u;
    uint qx = quad % WARN_GRID;
    uint qz = quad / WARN_GRID;
    float2 uv = (float2((float) qx, (float) qz) + kCorner[vid % 6u]) / (float) WARN_GRID * 2.0 - 1.0;

    SwarmWarnCircle w = circles[iid];
    float radius = max(w.radius, 0.01);

    // ---- 帯（ボスの衝撃波）: 同じ 64 枚の四角を円周に沿って 64 個の区間に
    // 並べ、帯を横切る方向は 1 枚分。どの頂点も真下の地面に乗る
    // （中心の高さへのクランプはしない: 25 m の輪は丘ごと跨ぐ）。PS から見ると
    // r = 1 - edgeWidth .. 1、つまり帯全体が縁 ----
    if (w.band > 0.0)
    {
        float2 c = kCorner[vid % 6u];
        float a = ((float) quad + c.x) / (float) (WARN_GRID * WARN_GRID) * 6.2831853;
        float half = 0.5 * w.band;
        float rr = radius - half + c.y * w.band;
        float2 bxz = w.center.xz + float2(cos(a), sin(a)) * max(rr, 0.0);
        float bh = SwarmTerrainHeight(heights, bxz);
        o.pos = mul(mul(float4(bxz.x, bh + g_RingLift, bxz.y, 1.0), g_RingView), g_RingProj);
        float ew = 0.999;
        o.edgeWidth = ew;
        o.uv = float2(lerp(1.0 - ew, 1.0, c.y), 0.0);
        o.progress = -1.0;
        return o;
    }

    // ---- 扇形（炎の息）/ 帯（矢、突進）、2026-10-25: 8 x 8 の四角を形に沿って並べ、どの頂点も
    // 地面に乗る。縁は r =「どこまで進んだか」と「辺にどれだけ近いか」の大きい方を使うので、
    // 輪郭に沿って一周する。塗りは「どこまで進んだか」だけで伸びる（shapeFill.y）: r だと辺が
    // 半径ほど離れた所からしか始まらず、長い帯では溜めの最後の最後、扇形では 2/3 の所からだった ----
    if (w.shape != 0.0)
    {
        float2 c = kCorner[vid % 6u];
        float u = ((float) qx + c.x) / (float) WARN_GRID;   // 横方向 0..1
        float f = ((float) qz + c.y) / (float) WARN_GRID;   // 進行方向 0..1
        float ys, yc;
        float2 sxz;
        float sideDist;
        if (w.shape > 0.0)
        {
            float a = w.yaw + (u * 2.0 - 1.0) * w.shape;
            sincos(a, ys, yc);
            sxz = w.center.xz + float2(ys, yc) * (f * radius);
            sideDist = f * radius * sin(w.shape * (1.0 - abs(u * 2.0 - 1.0)));
        }
        else
        {
            float halfW = -0.5 * w.shape;
            sincos(w.yaw, ys, yc);
            float across = (u * 2.0 - 1.0) * halfW;
            sxz = w.center.xz + float2(ys, yc) * (f * radius) + float2(yc, -ys) * across;
            sideDist = halfW - abs(across);
        }
        float sh = SwarmTerrainHeight(heights, sxz);
        o.pos = mul(mul(float4(sxz.x, sh + g_RingLift, sxz.y, 1.0), g_RingView), g_RingProj);
        o.uv = float2(max(f, 1.0 - sideDist / radius), 0.0);
        o.progress = (w.progress < 0.0) ? -1.0 : saturate(w.progress);
        o.edgeWidth = g_RingEdgeWidth / radius;
        o.shapeFill = float2(1.0, f);
        return o;
    }

    float2 xz = w.center.xz + uv * radius;
    float h = SwarmTerrainHeight(heights, xz);
    h = clamp(h, w.center.y - WARN_MAX_STEP, w.center.y + WARN_MAX_STEP);
    float3 world = float3(xz.x, h + g_RingLift, xz.y);

    o.pos = mul(mul(float4(world, 1.0), g_RingView), g_RingProj);
    o.uv = uv;
    o.progress = (w.progress < 0.0) ? -1.0 : saturate(w.progress);
    o.edgeWidth = g_RingEdgeWidth / radius;
    return o;
}
