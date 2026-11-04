// ============================================================
// GrassCullCS.hlsl
// カメラ周りの正方形の窓の中の格子セル 1 つにつき 1 スレッド。
// 各セルには葉が 1 枚生えうる（セル内でハッシュによりずらす）。残した
// 葉は Append バッファに入れ、GrassVS が DrawInstancedIndirect で
// 描く。
//
// 捨てるもの: g_MaxDist より遠いもの、距離による間引き（残った葉は
// 幅を広げる。間引かれる直前の葉は先に縮めるので、急に消えたり
// しない）、草の無いセル（土の坂、外周、上がれない台地: 地面テクスチャの
// アルファ）、急な地面（崖面。高さ場から判定）、
// 視錐台の外。
//
// 2026-10-23 からは残した葉のアニメーションもここで行う。頂点ごとでなく
// 葉ごとに 1 回（葉の頂点は 9 から 21 に増えた）:
//   世界の風      流れていく 2 つの突風の波（大きい波と、別方向からの
//                 小さい波）。強い突風は葉を裏返し、明るい裏面が
//                 見える（sheen）
//   風源          （竜巻、渦、砂旋、突風）は葉を流れに沿って寝かせる。
//                 芯の近くで空気が上昇する所では、先端が芯の方へ
//                 巻き上がり、ねじれて震える
//   踏みつけ      マップ（プレイヤー、敵、爆発、光線、突風、そして
//                 風源が後に残す渦の跡）
//   火            燃料が尽きると葉が縮んで焦げ、熱で光る
// 葉の背骨は 3 次ベジェ（root, c1, c2, tip）。近くに風源が無ければ
// 旧来の曲線そのもの: 先端のずれ ~ t^2、高さ ~ t。
//
// 2026-10-25: Append する葉は最大 g_MaxBlades 枚（バッファの
// サイズ）。各グループが残した葉を共有メモリで数え、1 スレッドが
// その数のスロットを g_BladeBudget で予約し、予算内の葉だけを
// Append する。以前は間隔を細かくすると Append のカウンタ（= 描画する
// インスタンス数）がバッファを超えて増え続けていた。
// ============================================================
#include "GrassCommon.hlsli"
#include "../Common/WindSources.hlsli"

Texture2D<float> g_Height : register(t0);  // GridWorld の高さ。テクセル中心 = 高さセルの中心
Texture2D<float4> g_Ground : register(t1); // rgb = 地面の色（リニア）、a = 1 草あり / 0 無し
Texture2D<float2> g_Trample : register(t2);
Texture2D<float2> g_Fire : register(t3);   // x = 温度、y = 残りの燃料（0..1）
StructuredBuffer<WindSource> g_WindSources : register(t4);   // 局所的な風（竜巻、渦、砂旋、突風）
Texture2D<float> g_DryPatch : register(t5);  // 1 = 燃える枯れ草（GrassFireField）。g_FireOn の間だけバインド
SamplerState s_Linear : register(s0);      // クランプ
AppendStructuredBuffer<GrassBlade> g_OutBlades : register(u0);
RWBuffer<uint> g_BladeBudget : register(u1);   // [0] = このフレームで予約済みのスロット数（CPU が 0 にクリア）

groupshared uint gs_Kept;   // このグループが残した葉の数
groupshared uint gs_Base;   // このグループが予約した最初のスロット

// 流れていく波 1 つの 0..1: ほとんど凪いでいて、波頭だけ鋭い
float GustWave(float2 xz, float2 dir, float perMeter, float speed)
{
    float g = sin(dot(xz, dir) * perMeter * 6.2831853 - g_Time * speed) * 0.5 + 0.5;
    return g * g;
}

// 原点から始まる 3 次ベジェのおおよその弧長
float BezierLength(float3 c1, float3 c2, float3 tip)
{
    float chord = length(tip);
    float net = length(c1) + length(c2 - c1) + length(tip - c2);
    return (chord + net) * 0.5;
}

// セルに見える葉があると分かった後で、葉の曲がりと見た目を決める（b には pos / height / width /
// shade が入っている）
void FinishBlade(float2 xz, float2 uv, float yaw, float curl, float rTwist, float rDry, float rPhase, inout GrassBlade b)
{
    float2 side = float2(cos(yaw), sin(yaw));
    float2 facing = float2(-side.y, side.x);

    // ---- 火: 燃えた草は短く黒く、熱いセルは先端が光る ----
    b.burn = 0.0;
    b.glow = 0.0;
    if (g_FireOn > 0.5)
    {
        float2 fire = g_Fire.SampleLevel(s_Linear, uv, 0);
        b.burn = saturate(1.0 - fire.y);
        b.glow = saturate((fire.x - g_FireIgnite * 0.7) * 1.5);
        b.height *= 1.0 - g_FireShrink * b.burn;
    }
    float h = b.height;

    // ---- 世界の風: 大きい突風の波 + それと交差する小さい波 ----
    float g1 = GustWave(xz, g_WindDir, g_WindScale, g_WindSpeed);
    float g2 = GustWave(xz, g_Wave2.xy, g_Wave2.z, g_Wave2.w);
    float gust = g1 + g_BladeShape.z * g2 * (1.0 - g1);
    float flutter = sin(g_Time * 3.1 + yaw * 5.0) * 0.15 + sin(g_Time * 9.3 + rPhase * 6.2831853) * 0.06 * gust;
    float2 bend = facing * curl
        + g_WindDir * (g_WindStrength * (0.25 + gust + flutter))
        + g_Trample.SampleLevel(s_Linear, uv, 0) * g_TrampleBend;

    // ---- 風源: 流れに沿って寝かせ、流れと直角に震わせる。上昇する芯の近くでは先端が巻き上がる ----
    float lift;
    float2 toCore;
    float3 air = WindSourceAirCore(g_WindSources, xz, lift, toCore);
    float spd = length(air.xz);
    float2 flow = spd > 1e-3 ? air.xz / spd : facing;
    float2 across = float2(-flow.y, flow.x);
    float lay = saturate(spd / max(g_Vortex.x, 0.1));
    float curlUp = saturate(air.y / max(g_Vortex.y, 0.1)) * lay;
    float shiver = sin(g_Time * (17.0 + 9.0 * rPhase) + rPhase * 40.0) * g_Vortex.z * saturate(spd / 10.0);
    bend += flow * (0.95 * lay) + across * shiver;

    float bl = length(bend);
    if (bl > 0.95)
    {
        bend *= 0.95 / bl;
        bl = 0.95;
    }
    float tipY = sqrt(max(1.0 - bl * bl, 0.0)) * h;

    // 旧来の曲線（ずれ ~ t^2、高さ ~ t）を 3 次ベジェで表す
    float3 tipA = float3(bend.x * h, tipY, bend.y * h);
    float3 c1A = float3(0.0, tipY / 3.0, 0.0);
    float3 c2A = float3(tipA.x / 3.0, tipY * (2.0 / 3.0), tipA.z / 3.0);
    // 巻き上がり: 根元で流れに沿って倒れ、そこから芯へ向かって急に吸い上げられる
    // （高いカメラからは「竜巻に引き上げられている」ように見える）。流れと直角に震える
    float3 f3 = float3(flow.x, 0.0, flow.y);
    float3 k3 = float3(toCore.x, 0.0, toCore.y);
    float3 a3 = float3(across.x, 0.0, across.y);
    float3 c1C = (f3 * 0.32 + float3(0.0, 0.22, 0.0)) * h;
    float3 c2C = (f3 * 0.45 + k3 * 0.12 + float3(0.0, 0.55, 0.0)) * h;
    float3 tipC = (f3 * 0.38 + k3 * 0.28 + float3(0.0, 0.85, 0.0) + a3 * (shiver * 2.0)) * h;
    float3 c1 = lerp(c1A, c1C, curlUp);
    float3 c2 = lerp(c2A, c2C, curlUp);
    float3 tip = lerp(tipA, tipC, curlUp);
    // 長さ: 旧来の曲線はその長さのまま、巻き上がった方は葉の高さに合わせる
    float k = lerp(BezierLength(c1A, c2A, tipA), h * 1.02, curlUp) / max(BezierLength(c1, c2, tip), 1e-4);
    b.c1 = c1 * k;
    b.c2 = c2 * k;
    b.tip = tip * k;

    // ---- 見た目 ----
    // 強い突風は葉を裏返す（明るい裏面。フィールドを帯が転がっていく）。
    // 風源の中の葉も同じように見せる
    b.sheen = smoothstep(0.45, 1.0, gust) * saturate(g_WindStrength / 0.35) * 0.8;
    b.sheen = max(b.sheen, max(curlUp, lay * 0.6));
    b.twist = (rTwist * 2.0 - 1.0) * g_BladeShape.x + curlUp * g_Vortex.w * (rTwist < 0.5 ? -1.0 : 1.0);
    b.dry = rDry < g_BladeShape.y ? 0.55 + 0.45 * (rDry / max(g_BladeShape.y, 1e-3)) : 0.0;
    if (g_FireOn > 0.5)
    {
        float patch = g_DryPatch.SampleLevel(s_Linear, uv, 0);
        if (patch > 0.01)
            b.dry = 1.0 + patch;   // 1..2 = 藁色（GrassPS）: これは燃える
    }
    b.side = side;
    b.pad = float2(0.0, 0.0);
}

// 格子セル id の葉（false = ここに葉は無い: 窓の外、間引き、草無し、急すぎる、
// 視界の外）。出口は 1 つだけ: 途中で return すると fxc が呼び出し側で X4000 を出す
bool MakeBlade(uint3 id, out GrassBlade b)
{
    b = (GrassBlade) 0;
    bool ok = false;
    if (id.x < g_CellCount.x && id.y < g_CellCount.y)
    {
        int2 cell = g_CellMin + int2(id.xy);
        uint seed = GrassHash(asuint(cell.x) * 73856093u ^ GrassHash(asuint(cell.y) * 19349663u));
        float jx = GrassRand(seed);
        float jz = GrassRand(seed);
        float2 xz = (float2(cell) + float2(jx, jz)) * g_Spacing;

        float dist = distance(xz, g_CamPos.xz);
        float density = lerp(1.0, g_FarDensity, saturate((dist - g_FullDist) / max(g_MaxDist - g_FullDist, 1e-3)));
        float keep = GrassRand(seed);
        float2 uv = (xz - g_MapOrigin) / g_MapSize;
        if (dist <= g_MaxDist && keep <= density && all(uv > 0.0) && all(uv < 1.0))
        {
            uint tw, th;
            g_Ground.GetDimensions(tw, th);
            float4 ground = g_Ground.Load(int3(int2(uv * float2(tw, th)), 0));
            if (ground.a >= 0.5)
            {
                // 周囲 0.3m の双線形の高さから急さを求める（崖の縁は高さのテクセル 1 つ分の幅）
                float y = g_Height.SampleLevel(s_Linear, uv, 0);
                const float e = 0.3;
                float2 du = float2(e / g_MapSize.x, 0.0);
                float2 dv = float2(0.0, e / g_MapSize.y);
                float hx0 = g_Height.SampleLevel(s_Linear, uv - du, 0);
                float hx1 = g_Height.SampleLevel(s_Linear, uv + du, 0);
                float hz0 = g_Height.SampleLevel(s_Linear, uv - dv, 0);
                float hz1 = g_Height.SampleLevel(s_Linear, uv + dv, 0);
                float rise = max(max(abs(hx1 - y), abs(y - hx0)), max(abs(hz1 - y), abs(y - hz0)));
                if (rise <= g_MaxSlope * e)
                {
                    b.pos = float3(xz.x, y, xz.y);
                    b.height = lerp(g_HeightMin, g_HeightMax, GrassRand(seed)) * saturate((density - keep) / 0.08);
                    float yaw = GrassRand(seed) * 6.2831853;
                    b.width = lerp(g_WidthMin, g_WidthMax, GrassRand(seed)) * min(rsqrt(max(density, 0.05)), 3.0);
                    float curl = GrassRand(seed) * g_CurlMax;
                    b.shade = 0.85 + 0.3 * GrassRand(seed);
                    // 旧来の乱数の後に引くので、どの葉も 2026-10-23 以前のサイズ / 向き / 曲がりを保つ
                    float rTwist = GrassRand(seed);
                    float rDry = GrassRand(seed);
                    float rPhase = GrassRand(seed);

                    // 葉を囲む球（先端は横へ最大で高さ 1 つ分曲がる）
                    float3 c = b.pos + float3(0.0, b.height * 0.5, 0.0);
                    float r = b.height + b.width;
                    bool inView = true;
                    [unroll]
                    for (uint i = 0u; i < 6u; ++i)
                        inView = inView && dot(g_Frustum[i].xyz, c) + g_Frustum[i].w >= -r;
                    if (inView)
                    {
                        FinishBlade(xz, uv, yaw, curl, rTwist, rDry, rPhase, b);
                        ok = true;
                    }
                }
            }
        }
    }
    return ok;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    if (gi == 0u)
        gs_Kept = 0u;
    GroupMemoryBarrierWithGroupSync();

    GrassBlade b;
    bool kept = MakeBlade(id, b);
    uint local = 0u;   // グループ内で残した葉のうち、この葉の順番
    if (kept)
        InterlockedAdd(gs_Kept, 1u, local);
    GroupMemoryBarrierWithGroupSync();

    // 予約はグループごとに 1 回（葉ごとにアトミックを打たない）
    if (gi == 0u)
    {
        uint base;
        InterlockedAdd(g_BladeBudget[0], gs_Kept, base);
        gs_Base = base;
    }
    GroupMemoryBarrierWithGroupSync();

    // バッファのサイズを超えた葉は捨てるので、描画数がそれを超えることは無い
    if (kept && gs_Base + local < g_MaxBlades)
        g_OutBlades.Append(b);
}
