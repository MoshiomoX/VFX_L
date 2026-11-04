// ============================================================
// WindSources.hlsli
// 局所的な風源（2026-10-19、ユーザー:「風そのものが風の係数を持って
// 世界の風に作用してほしい」）。砂旋（と、今後吹くもの全般）は毎フレーム
// SwarmWindSourceCS が WIND_MAX_SOURCES 個の固定の表に書き込む。
// 使っていない項目は radius 0。
// 草（GrassVS）、地面の落ち葉（DebrisUpdateCS）、パーティクル
// （ParticleUpdateCS）は WindSourceAir() を世界の風に足すので、竜巻 1 つで
// 草が円を描いて曲がり、フィールドの砂やごみが舞い上がり、
// 周りの煙が引きずられる。
// 2026-10-20: 風源は空気を一方向に押すこともできる（pushDir x push、
// 中心で最大、縁で 0）- 突風の魔法は前へ吹く渦。CPU の風源
// （SwarmSystem::AddWindBurst）も同じ CS で表の後ろに
// 追加する。
// C++ 側: Swarm::WindSource（48 バイト）、Swarm::kMaxWindSources
// ============================================================
#ifndef WIND_SOURCES_HLSLI
#define WIND_SOURCES_HLSLI

#define WIND_MAX_SOURCES 16u

struct WindSource
{
    float3 pos;      // 地面上の中心
    float radius;    // 届く距離（m）。0 = 空きスロット
    float swirl;     // 芯の縁での接線方向の速さ（m/s、上から見て反時計回り）
    float updraft;   // 芯の近くの上昇速度（m/s）
    float inflow;    // 縁での中心へ向かう速さ（m/s）
    float fade;      // 0..1（出現 / 消滅）
    float2 pushDir;  // 単位 xz: 空気をこの向きに吹き流す（突風）。0 = 無し
    float push;      // 中心での m/s
    float _pad;
};

// 地面上の点 xz（y = 0 平面）での全風源の風速。lift = 一番強い影響 0..1
float3 WindSourceAir(StructuredBuffer<WindSource> sources, float2 xz, out float lift)
{
    float3 air = 0;
    lift = 0.0;
    [loop]
    for (uint k = 0u; k < WIND_MAX_SOURCES; ++k)
    {
        WindSource w = sources[k];
        if (w.radius <= 0.0)
            continue;
        float2 d = xz - w.pos.xz;
        float dist = length(d);
        if (dist >= w.radius)
            continue;
        float r = dist / w.radius;
        float2 outw = dist > 1e-3 ? d / dist : float2(0, 1);
        float2 tang = float2(-outw.y, outw.x);
        // 渦は外へ 3 割ほどの所で最大になり縁へ向かって弱まる。上昇気流は芯だけのもの
        float sw = saturate(r / 0.3) * saturate((1.0 - r) / 0.7);
        float up = saturate((1.0 - r) / 0.6);
        float inw = saturate((r - 0.25) / 0.75) * (1.0 - r);
        float ps = 1.0 - r * r;   // 押す力: 中心で最大、縁で 0
        air += float3(tang.x, 0.0, tang.y) * (w.swirl * sw * w.fade)
             + float3(0.0, w.updraft * up * w.fade, 0.0)
             - float3(outw.x, 0.0, outw.y) * (w.inflow * inw * w.fade)
             + float3(w.pushDir.x, 0.0, w.pushDir.y) * (w.push * ps * w.fade);
        lift = max(lift, (1.0 - r) * w.fade);
    }
    return air;
}

// 同じ風速に加え、ここで一番強く効く風源の芯への向き（単位 xz）を返す
// （2026-10-23: 竜巻の近くの草の葉は先端をそちらへ巻き込む）
float3 WindSourceAirCore(StructuredBuffer<WindSource> sources, float2 xz, out float lift, out float2 toCore)
{
    float3 air = WindSourceAir(sources, xz, lift);
    toCore = float2(0.0, 0.0);
    float best = 0.0;
    [loop]
    for (uint k = 0u; k < WIND_MAX_SOURCES; ++k)
    {
        WindSource w = sources[k];
        float2 d = w.pos.xz - xz;
        float dist = length(d);
        float s = w.radius > 0.0 ? saturate(1.0 - dist / max(w.radius, 1e-3)) * w.fade : 0.0;
        if (s > best && dist > 1e-3)
        {
            best = s;
            toCore = d / dist;
        }
    }
    return air;
}

#endif
