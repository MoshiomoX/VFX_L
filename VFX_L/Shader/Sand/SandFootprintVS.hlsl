// ============================================================
// SandFootprintVS.hlsl
// インスタンス 1 つにつき足跡 1 つ（DrawInstanced(6, n)、頂点バッファなし）：
// 地面に寝かせた板で、歩く向きに沿い、地面の法線に傾け、
// 押し上げられた砂の盛り上がりが収まるよう靴底より少し大きい。
// 砂の状態（SandField）は中心で読む：湿った砂は
// 跡が深く残り、風が吹き上げている砂（w）は跡が薄れる。
// ============================================================
#include "SandFootprintCommon.hlsli"

// = SandFootprints::GpuPrint（48 バイト）
struct FootPrint
{
    float3 pos;
    float yaw;
    float3 normal;
    float alpha;
    float side;     // 1 = 右、-1 = 左（土踏まずを左右反転する）
    float length;
    float width;
    float pad;
};

StructuredBuffer<FootPrint> g_Prints : register(t0);
Texture2D<float4> g_State : register(t1);   // SandField：x 温度、y 乾燥度、z 陽炎、w 舞い上がり

static const float2 kQuad[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

FootOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    FootPrint f = g_Prints[iid];
    float2 q = kQuad[vid] * FOOT_CARD_PAD;

    float3 n = normalize(f.normal);
    float3 fw = float3(sin(f.yaw), 0.0, cos(f.yaw));
    fw = normalize(fw - n * dot(fw, n));
    float3 right = cross(n, fw);

    float3 world = f.pos + n * g_Raise
                 + right * (q.x * f.width * 0.5)
                 + fw * (q.y * f.length * 0.5);

    float a = f.alpha;
    if (g_HasState > 0.5)
    {
        uint w, h;
        g_State.GetDimensions(w, h);
        float2 t = (f.pos.xz - g_MapOrigin) / g_MapSize;
        int2 p = clamp(int2(t * float2(w, h)), int2(0, 0), int2(w - 1, h - 1));
        float4 s = g_State.Load(int3(p, 0));
        a *= lerp(g_WetMul, 1.0, saturate(s.y));   // 湿っている = 跡が深い
        a *= 1.0 - 0.7 * saturate(s.w);            // 吹き上がる砂はすぐにぼかす
    }

    FootOut o;
    o.pos = mul(float4(world, 1.0), g_ViewProj);
    o.uv = float2(q.x * f.side, q.y);
    o.alpha = a;
    float3 L = normalize(g_SunDir);
    o.sun = float3(dot(L, right) * f.side, dot(L, fw), dot(L, n));
    o.halfSize = float2(f.width, f.length) * 0.5;
    o.grainUV = world.xz;
    return o;
}
