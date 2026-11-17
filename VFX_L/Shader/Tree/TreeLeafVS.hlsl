// ============================================================
// TreeLeafVS.hlsl
// インスタンス 1 つにつき葉 1 枚（DrawInstanced(6, 葉の数)、頂点バッファなし）。
// 付け根を TreeDeform で動かし、葉の板を葉柄の軸の周りにはためかせ、風下へ少し倒す。
// 陰影の法線は板の向きと樹冠の球の法線を混ぜる（遠目に樹冠が 1 つの塊に見える）。
// 影の段でも同じ VS（g_ViewProj が光源の行列、遠いカスケードは g_LeafStride 枚おきに大きくして描く）
// ============================================================
#include "TreeCommon.hlsli"
#include "../Common/MathConst.hlsli"

StructuredBuffer<TreeLeaf> g_Leaves : register(t0);
StructuredBuffer<TreeState> g_States : register(t1);

struct LeafOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;      // x = 付け根 0 → 先 1、y = -1..1（幅方向）
    nointerpolation uint seed : TEXCOORD3;
    nointerpolation float shade : TEXCOORD4;
};

static const float2 kQuad[6] =
{
    float2(0, -1), float2(1, -1), float2(0, 1),
    float2(0, 1), float2(1, -1), float2(1, 1)
};

LeafOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    uint li = iid * g_LeafStride;
    if (li >= g_LeafCount)
    {
        LeafOut z = (LeafOut) 0;
        z.pos = float4(0.0, 0.0, -1.0, 1.0);   // クリップで捨てる
        return z;
    }
    TreeLeaf l = g_Leaves[li];
    TreeState t = g_States[l.tree];
    float3 base = TreeDeform(l.pos, t, l.mainW, l.branchW, l.phase);

    uint s = l.seed;
    float r1 = TreeRand(s), r2 = TreeRand(s), r3 = TreeRand(s);

    // 葉の板：葉柄の軸 T の周りの向き（ロール）をはためかせる
    float3 T = l.tipDir;
    float3 ax = abs(T.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 n0 = normalize(cross(T, ax));
    float3 n1 = cross(T, n0);
    float amt = g_FlutterIdle + g_FlutterWind * t.windAmt;
    float roll = r1 * TWO_PI + sin(g_Time * (6.0 + 5.0 * r2) + r3 * TWO_PI) * amt;
    float3 N = n0 * cos(roll) + n1 * sin(roll);

    // 風下へ倒す（強い風ほど、少しずつ揺れながら）
    float airLen = length(t.air);
    float2 wd = airLen > 1e-3 ? t.air / airLen : float2(1.0, 0.0);
    float bend = saturate(0.3 * t.windAmt) * (0.6 + 0.4 * sin(g_Time * (4.0 + 3.0 * r2) + r1 * TWO_PI));
    T = normalize(T + float3(wd.x, 0.0, wd.y) * bend);
    float3 S = normalize(cross(N, T));
    N = cross(T, S);

    float2 q = kQuad[vid];
    float L = l.size * g_LeafScale;
    float W = L * g_LeafWidth * 0.5;
    float3 world = base + T * (q.x * L) + S * (q.y * W);

    if (dot(N, g_CamPos - world) < 0.0)
        N = -N;

    LeafOut o;
    o.world = world;
    o.normal = normalize(lerp(N, l.crownN, g_CrownMix));
    o.uv = q;
    o.seed = l.seed;
    o.shade = l.shade;
    o.pos = mul(float4(world, 1.0), g_ViewProj);
    return o;
}
