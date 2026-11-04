// ============================================================
// DandelionPS.hlsl
// タンポポの各部品を板から切り抜き、他のモデルと同じように照らす
// （Lighting.hlsli：トゥーンの段、太陽の影、点光源、
// 霧）。不透明で深度を書く（草と一緒に、アウトラインの後に描く）。
//   stem : 頭へ向かって細くなる帯
//   head : 白いふわふわの球（縁は冠毛の房でギザギザ、中央に小さな
//          茶色の種の塊）。球として照らす
//   seed : 茶色の点のまわりに冠毛が放射状に伸びた小さな星
// DandelionCB が b0 を使うので、ライトの cbuffer は b1 に置く。
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "DandelionCommon.hlsli"

struct DandelionOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint part : TEXCOORD3;
    nointerpolation uint seed : TEXCOORD4;
};

float4 main(DandelionOut i) : SV_TARGET
{
    float3 albedo;
    float3 n = normalize(i.normal);
    float r = length(i.uv);
    float ang = atan2(i.uv.y, i.uv.x);
    float var = (float) (i.seed & 0xFFu) / 255.0;

    if (i.part == 0u)
    {
        float t = i.uv.y * 0.5 + 0.5;
        float w = 1.0 - 0.45 * t;
        if (abs(i.uv.x) > w)
            discard;
        albedo = g_StemColor * (0.85 + 0.3 * var) * lerp(0.8, 1.05, t);
    }
    else if (i.part == 1u)
    {
        // ギザギザの縁：縁まわりの房（周波数を 2 つ使い、頭ごとに形を変える）
        float tufts = 0.5 + 0.5 * cos(ang * 13.0 + var * 6.28) * cos(ang * 5.0 + var * 17.0);
        float rim = 0.78 + 0.2 * tufts;
        if (r > rim)
            discard;
        // 円盤から球の法線を作る：カメラを向いた板を球の代わりにする
        float3 toCam = n;
        float3 sphereN = normalize(g_CamRight * i.uv.x + g_CamUp * i.uv.y + toCam * sqrt(saturate(1.0 - r * r)));
        n = sphereN;
        albedo = g_PuffColor * (0.9 + 0.1 * var);
        // 放射状に伸びる冠毛の細い筋
        albedo *= 0.9 + 0.1 * (0.5 + 0.5 * cos(ang * 41.0 + var * 3.0));
        if (r < 0.16)
            albedo = float3(0.42, 0.33, 0.16);   // 中央の種の塊
    }
    else
    {
        // 冠毛の星：細い 6 本の放射線 + 種の点
        float ray = abs(sin(ang * 3.0 + var * 6.28)) * r;
        bool keep = (r < 0.28) || (ray < 0.13 && r < 1.0);
        if (!keep)
            discard;
        albedo = (r < 0.2) ? float3(0.42, 0.33, 0.16) : g_PuffColor;
    }

    float3 lit = ShadeLambert(n, albedo, i.world);
    return float4(lit, 1.0);
}
