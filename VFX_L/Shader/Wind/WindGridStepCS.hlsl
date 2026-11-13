// ============================================================
// WindGridStepCS.hlsl
// 風の格子（World/WindGrid）の 1 歩目：前のフレームの格子から
//   1. 移流：自分の速さ（+ 天気の風の一部）で後ろへ辿った所の値を取る（半ラグランジュ。前の格子の原点からの位置で引くので、
//      格子がプレイヤーに付いて動いても同じ式で済む。前の格子の外は 0）
//   2. モーター：風源の表（竜巻・渦・突風・砂旋）の式の風へ、風源の効く所ほど速く寄せる（g_Relax）
//   3. 減衰：風源の効いていない所は g_Decay で 0 へ戻る（= 突風が止んだ後の余韻）
// 圧力（非圧縮）は解かない（重い割に違いが見えない）。この後 WindGridDiffuseCS が数回ぼかす
// 高さ：足元（g_FeetY）から g_HeightFull m までは風源をそのまま、g_HeightZero m で 0
// ============================================================
#include "../Common/WindSources.hlsli"

cbuffer WindGridCB : register(b0)
{
    float3 g_Origin;      // このフレームの格子の角（m）
    float g_Cell;         // 1 マスの m
    float3 g_PrevOrigin;  // 前のフレームの格子の角
    float g_Dt;
    float g_Decay;        // 1/秒（風源の無い所）
    float g_Relax;        // 1/秒（風源の効く所で式の風へ寄せる速さ）
    float g_HeightFull;   // 足元からこの m までは風源をそのまま
    float g_HeightZero;   // この m で 0
    float2 g_AdvectWind;  // 移流に足す天気の風（xz、m/s）
    float g_FeetY;        // プレイヤーの足元の高さ
    float g_PrevValid;    // 0 = 前の格子は無い（最初のフレーム・作り直し）
    int3 g_Dims;
    float g_MotorGain;
};

StructuredBuffer<WindSource> g_WindSources : register(t0);
Texture3D<float4> g_Prev : register(t1);
RWTexture3D<float4> g_Out : register(u0);

// 前の格子を世界の位置で引く（三線形）。前の格子の外は 0（新しく入ってきた所は無風から）
float3 SamplePrev(float3 wpos)
{
    float3 f = (wpos - g_PrevOrigin) / g_Cell;
    float3 dims = (float3)g_Dims;
    float3 result = float3(0.0, 0.0, 0.0);
    if (g_PrevValid > 0.5 && all(f >= 0.0) && all(f <= dims))
    {
        f -= 0.5;
        int3 i0 = (int3)floor(f);
        float3 t = f - (float3)i0;
        int3 a = clamp(i0, int3(0, 0, 0), g_Dims - 1);
        int3 b = clamp(i0 + 1, int3(0, 0, 0), g_Dims - 1);
        float3 c000 = g_Prev.Load(int4(a.x, a.y, a.z, 0)).xyz;
        float3 c100 = g_Prev.Load(int4(b.x, a.y, a.z, 0)).xyz;
        float3 c010 = g_Prev.Load(int4(a.x, b.y, a.z, 0)).xyz;
        float3 c110 = g_Prev.Load(int4(b.x, b.y, a.z, 0)).xyz;
        float3 c001 = g_Prev.Load(int4(a.x, a.y, b.z, 0)).xyz;
        float3 c101 = g_Prev.Load(int4(b.x, a.y, b.z, 0)).xyz;
        float3 c011 = g_Prev.Load(int4(a.x, b.y, b.z, 0)).xyz;
        float3 c111 = g_Prev.Load(int4(b.x, b.y, b.z, 0)).xyz;
        float3 x00 = lerp(c000, c100, t.x), x10 = lerp(c010, c110, t.x);
        float3 x01 = lerp(c001, c101, t.x), x11 = lerp(c011, c111, t.x);
        result = lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
    }
    return result;
}

[numthreads(8, 4, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= (uint3)g_Dims))
        return;
    float3 wpos = g_Origin + ((float3)id + 0.5) * g_Cell;

    // ---- 1. 移流（自分の速さで後ろへ辿る）----
    float3 v0 = SamplePrev(wpos);
    float3 back = wpos - (v0 + float3(g_AdvectWind.x, 0.0, g_AdvectWind.y)) * g_Dt;
    float3 v = SamplePrev(back);

    // ---- 2. モーター：風源の効く所ほど式の風へ寄せる ----
    float lift;
    float3 air = WindSourceAir(g_WindSources, wpos.xz, lift) * g_MotorGain;
    float h = wpos.y - g_FeetY;
    float hw = saturate((g_HeightZero - h) / max(g_HeightZero - g_HeightFull, 1e-3));
    float infl = saturate(lift * 3.0) * hw;   // 風源の芯〜縁の手前まで 1、縁で 0
    v = lerp(v, air * hw, 1.0 - exp(-g_Dt * g_Relax * infl));

    // ---- 3. 減衰（風源の効いていない所ほど速く 0 へ）----
    v *= exp(-g_Dt * g_Decay * (1.0 - infl));

    g_Out[id] = float4(v, 0.0);
}
