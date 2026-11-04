// ============================================================
// DebrisVS.hlsl
// インスタンス 1 つにつき地面の落ち葉 1 片（DrawInstanced(6, side^2)、
// 頂点バッファなし）：小さな板（三角形 2 枚）を片の
// オイラー角で回したもの。静止中は地面に平らに置かれ、空中では
// 転がる。非表示の片と窓の端の片は縮んで
// 消える（端はカメラが動いた時に行が置き直される所）。
// PS が板から葉 / 草の形を切り抜く。
// ============================================================
#include "DebrisCommon.hlsli"

StructuredBuffer<Debris> g_Debris : register(t0);

struct DebrisOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;      // 板全体で -1..1（x 長さ方向、y 幅方向）
    nointerpolation uint seed : TEXCOORD3;
};

static const float2 kQuad[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

DebrisOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    Debris d = g_Debris[iid];
    DebrisOut o;
    o.seed = d.seed;
    o.uv = kQuad[vid];

    float2 size = DebrisSize(d.seed);
    // 窓の端でフェード（チェビシェフ距離：窓は正方形）
    float2 dc = abs(d.pos.xz - g_CamPos.xz);
    float edge = saturate((g_Window - max(dc.x, dc.y)) / max(g_FadeWidth, 0.01));
    float scale = edge;
    if ((d.seed & DEBRIS_HIDDEN) != 0u)
        scale = 0.0;

    float3x3 R = DebrisRotation(d.rot);
    float3 local = float3(o.uv.x * size.x * 0.5, 0.0, o.uv.y * size.y * 0.5) * scale;
    float3 world = d.pos + mul(R, local);
    float3 n = mul(R, float3(0, 1, 0));
    // 両面：カメラの方を向かせる
    if (dot(n, g_CamPos - world) < 0.0)
        n = -n;

    o.world = world;
    o.normal = n;
    o.pos = mul(float4(world, 1.0), g_ViewProj);
    return o;
}
