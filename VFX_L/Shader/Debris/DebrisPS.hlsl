// ============================================================
// DebrisPS.hlsl
// 板から破片の形を切り抜き（葉: 中央脈のある先のとがった楕円、
// 草くず / 小枝: 先細りの帯）、他のモデルと同じくライティングする
// （Lighting.hlsli: トゥーンの段階、太陽の影、点光源、フォグ）。
// 不透明で深度を書く（草と一緒に、アウトラインパスの後に描画）。
// DebrisCB が b0 を使うので、ライトの cbuffer は b1 に置く。
// ============================================================
#define MODEL_LIGHT_CB_REG b1
#include "../Common/Lighting.hlsli"
#include "DebrisCommon.hlsli"

struct DebrisOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint seed : TEXCOORD3;
};

float4 main(DebrisOut i) : SV_TARGET
{
    uint kind = i.seed & DEBRIS_KIND_MASK;
    float x = i.uv.x, y = abs(i.uv.y);
    float3 albedo;
    if (kind == 0u)
    {
        // 葉: +x 側を引き伸ばしてとがらせた楕円、中央脈は少し暗く
        float w = (1.0 - x * x) * (1.0 - 0.3 * x) * 1.05;
        if (y > w)
            discard;
        albedo = g_ColorLeaf;
        if (y < 0.07)
            albedo *= 0.78;
        // 縁を暗く
        albedo *= 1.0 - 0.25 * saturate((y - w + 0.25) / 0.25);
    }
    else
    {
        // 草くず / 小枝: +x に向かって細くなる帯
        float w = 1.0 - 0.45 * (x + 1.0) * 0.5;
        if (y > w)
            discard;
        albedo = (kind == 1u) ? g_ColorClip : g_ColorTwig;
    }
    // 破片ごとのばらつき: 明るさと、少し茶色寄りに
    float v = (float) ((i.seed >> 8) & 0xFFu) / 255.0;
    albedo *= 0.8 + 0.4 * v;
    albedo = lerp(albedo, albedo.rrr * float3(1.0, 0.75, 0.45), 0.35 * (float) ((i.seed >> 4) & 0x3u) / 3.0);

    float3 lit = ShadeLambert(normalize(i.normal), albedo, i.world);
    return float4(lit, 1.0);
}
