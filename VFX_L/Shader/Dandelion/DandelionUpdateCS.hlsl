// ============================================================
// DandelionUpdateCS.hlsl
// タンポポの綿毛の 1 ステップ（World/DandelionField、2026-10-22）。
// 花 1 つにつき 1 スレッド（自分の g_SeedsPer 個の種も動かす）:
//   1) 格子のセルが窓から出たら（または g_Reset）置き直す:
//      新しいセルの地面に満開の綿毛を出す。草の無い所、崖、
//      草原の斑の外では隠す。
//   2) 茎は綿毛の位置の空気の方へ曲がる（世界の風 + 局所的な
//      風源）。
//   3) 満開の綿毛は、局所的な風源、強い天気の風（確率で、株ごとに
//      しきい値が違う）、踏みつけ（草の踏みつけマップ: プレイヤー、爆発、
//      光線、突風の扇）で散る。
//      種は綿毛の位置から空気の速度で飛び出す。
//   4) 裸の茎は g_RegrowDelay 後に綿毛が生え直す。
//   5) 種: 空気の速度へ近づき（軽い: ゆっくり落ち、上昇気流で
//      持ち上がる）、少しさまよい、地面に落ちて消える。
// ============================================================
#include "DandelionCommon.hlsli"
#include "../Common/WindSources.hlsli"

Texture2D<float> g_Height : register(t0);          // GridWorld の高さ（GrassRenderer）
Texture2D<float4> g_Ground : register(t1);         // a = 草が生える
Texture2D<float2> g_Trample : register(t2);        // 草の踏みつけの倒れ（強さ = 長さ）
StructuredBuffer<WindSource> g_WindSources : register(t3);
SamplerState s_Linear : register(s0);
RWStructuredBuffer<Flower> g_Flowers : register(u0);
RWStructuredBuffer<Seed> g_Seeds : register(u1);

float2 MapUv(float2 xz)
{
    return (xz - g_MapOrigin) / g_MapSize;
}

float GroundAt(float2 xz)
{
    return g_Height.SampleLevel(s_Linear, MapUv(xz), 0);
}

// なめらかなバリューノイズ 0..1（草原の斑）
float PatchNoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    uint s = g_Seed * 2654435761u;
    float a = (float) (DandelionHash((uint) (int) i.x * 73856093u ^ (uint) (int) i.y * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    float b = (float) (DandelionHash((uint) (int) (i.x + 1.0) * 73856093u ^ (uint) (int) i.y * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    float c = (float) (DandelionHash((uint) (int) i.x * 73856093u ^ (uint) (int) (i.y + 1.0) * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    float d = (float) (DandelionHash((uint) (int) (i.x + 1.0) * 73856093u ^ (uint) (int) (i.y + 1.0) * 19349663u ^ s) & 0xFFFFu) / 65535.0;
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_Side * g_Side)
        return;

    Flower f = g_Flowers[i];
    uint seedBase = i * g_SeedsPer;

    // ---- 1) 今の窓の中の格子セル（トーラス状に巻き戻す。落ち葉と同じ） ----
    int side = (int) g_Side;
    int ix = (int) (i % g_Side), iz = (int) (i / g_Side);
    int2 base = (int2) floor((g_CamPos.xz - g_Window) / g_Spacing);
    int kx = ix - base.x, kz = iz - base.y;
    int cx = base.x + (kx - (int) floor((float) kx / (float) side) * side);
    int cz = base.y + (kz - (int) floor((float) kz / (float) side) * side);
    uint cell = ((uint) cx & 0xFFFFu) | (((uint) cz & 0xFFFFu) << 16);

    if (cell != f.cell || g_Reset != 0u)
    {
        uint s = DandelionHash((uint) cx * 83492791u ^ (uint) cz * 2971215073u ^ g_Seed);
        float2 xz = (float2(cx, cz) + float2(DandelionRand(s), DandelionRand(s))) * g_Spacing;
        float2 uv = MapUv(xz);
        uint tw, th;
        g_Height.GetDimensions(tw, th);
        float2 du = float2(1.0 / tw, 0), dv = float2(0, 1.0 / th);
        float y = g_Height.SampleLevel(s_Linear, uv, 0);
        float hx = g_Height.SampleLevel(s_Linear, uv + du, 0) - g_Height.SampleLevel(s_Linear, uv - du, 0);
        float hz = g_Height.SampleLevel(s_Linear, uv + dv, 0) - g_Height.SampleLevel(s_Linear, uv - dv, 0);
        float slope = length(float2(hx, hz)) / (2.0 * g_MapSize.x / tw);
        bool inside = all(uv > 0.0) && all(uv < 1.0);
        float grass = g_Ground.SampleLevel(s_Linear, uv, 0).a;
        // 斑: 2 オクターブ。ノイズの上位 g_PatchCover の割合を草原にする
        float n = PatchNoise(xz / max(g_PatchScale, 1.0)) * 0.7 + PatchNoise(xz / max(g_PatchScale * 0.37, 1.0) + 17.3) * 0.3;
        bool inPatch = n > 1.0 - g_PatchCover;

        f.pos = float3(xz.x, y, xz.y);
        f.puff = 1.0;
        f.bend = 0;
        f.regrow = 0.0;
        f.cell = cell;
        f.seed = s & 0xFFFFFFu;
        f._pad = 0;
        if (!inside || slope > g_MaxSlope || grass < 0.5 || !inPatch || DandelionRand(s) > g_Density)
            f.seed |= DANDELION_HIDDEN;
        g_Flowers[i] = f;
        for (uint k = 0u; k < g_SeedsPer; ++k)
        {
            Seed z = (Seed) 0;
            g_Seeds[seedBase + k] = z;
        }
        return;
    }

    if ((f.seed & DANDELION_HIDDEN) != 0u || g_Dt <= 0.0)
        return;

    uint rs = DandelionHash(f.seed ^ asuint(g_Time) ^ i * 2654435761u);

    // ---- 2) 綿毛の位置の空気: 世界の風 + 局所的な風源 ----
    float stem = DandelionStemHeight(f.seed);
    float2 wind = DandelionWindAt(f.pos.xz);
    float lift;
    float3 local = WindSourceAir(g_WindSources, f.pos.xz, lift);
    float2 airXZ = wind + local.xz;
    float2 bendGoal = airXZ * g_BendGain;
    float bl = length(bendGoal);
    if (bl > 0.6)
        bendGoal *= 0.6 / bl;   // 倒れるのは最大 30 度ほど
    f.bend = lerp(f.bend, bendGoal, saturate(g_Dt * 6.0));

    // ---- 3) 綿毛を散らす ----
    if (f.puff > 0.95)
    {
        bool release = false;
        float2 trample = g_Trample.SampleLevel(s_Linear, MapUv(f.pos.xz), 0);
        float localSpeed = length(local);
        // プレイヤーがかすめる: 草の踏みつけの円より広い円にして、群生を走り抜けると一筋に散らす
        float2 fromPlayer = f.pos.xz - g_PlayerPos.xz;
        float playerDist = length(fromPlayer);
        if (g_PlayerRadius > 0.0 && playerDist < g_PlayerRadius && abs(f.pos.y - g_PlayerPos.y) < 2.0)
        {
            release = true;
            trample = (playerDist > 1e-3 ? fromPlayer / playerDist : float2(0.0, 1.0)) * 0.8;
        }
        else if (length(trample) > g_ReleaseTrample)
            release = true;
        else if (localSpeed > g_ReleaseLocal && DandelionRand(rs) < saturate(localSpeed / g_ReleaseLocal - 0.6) * 8.0 * g_Dt)
            release = true;   // 風源が通り過ぎる: 近づくにつれて綿毛が次々に散る
        else
        {
            float w = length(wind);
            float thr = g_ReleaseWind * (0.8 + 0.5 * (float) ((f.seed >> 12) & 0xFFu) / 255.0);
            if (w > thr && DandelionRand(rs) < saturate((w - thr) / thr) * 0.8 * g_Dt)
                release = true;
        }
        if (release)
        {
            f.puff = 0.0;
            f.regrow = g_RegrowDelay * (0.7 + 0.6 * DandelionRand(rs));
            float3 top = f.pos + float3(f.bend.x * stem, stem, f.bend.y * stem);
            float3 air = float3(airXZ.x, local.y, airXZ.y) + float3(trample.x, 0.0, trample.y) * 3.0;
            for (uint k = 0u; k < g_SeedsPer; ++k)
            {
                Seed z;
                float a = DandelionRand(rs) * 6.2831853, c = DandelionRand(rs) * 2.0 - 1.0;
                float3 dir = float3(sqrt(1.0 - c * c) * cos(a), c, sqrt(1.0 - c * c) * sin(a));
                z.pos = top + dir * (g_PuffRadius * g_SizeMul * 0.8);
                z.vel = air * (0.5 + 0.5 * DandelionRand(rs)) + dir * 0.7 + float3(0.0, 0.5 + 0.7 * DandelionRand(rs), 0.0);
                z.age = 0.0;
                z.life = lerp(g_SeedLifeMin, g_SeedLifeMax, DandelionRand(rs));
                g_Seeds[seedBase + k] = z;
            }
        }
    }
    // ---- 4) 生え直し ----
    else
    {
        f.regrow -= g_Dt;
        if (f.regrow <= 0.0)
            f.puff = min(1.0, f.puff + g_Dt / max(g_RegrowTime, 0.1));
    }
    g_Flowers[i] = f;

    // ---- 5) 種 ----
    for (uint k = 0u; k < g_SeedsPer; ++k)
    {
        Seed z = g_Seeds[seedBase + k];
        if (z.life <= 0.0)
            continue;
        z.age += g_Dt;
        if (z.age >= z.life)
        {
            z.life = 0.0;
            g_Seeds[seedBase + k] = z;
            continue;
        }
        float ground = GroundAt(z.pos.xz);
        float above = z.pos.y - ground;
        float sl;
        float3 la = WindSourceAir(g_WindSources, z.pos.xz, sl);
        la *= saturate(1.0 - (above - 4.0) / 4.0);   // 風源は 8 m ほどより上では効かない（種を空まで飛ばさない）
        la.y *= g_SeedLift;
        float2 w2 = DandelionWindAt(z.pos.xz);
        // 少しさまよわせ、種の群れがひと塊で移動せず広がるようにする
        float ph = z.age * 1.7 + (float) (k * 7u + i) * 0.37;
        float3 wander = float3(sin(ph * 1.3), sin(ph * 0.9 + 1.7) * 0.5, cos(ph * 1.1)) * 0.6;
        float3 air = float3(w2.x, 0.0, w2.y) + la + wander;
        float3 acc = (air - z.vel) * g_SeedDrag + float3(0.0, -g_SeedGravity, 0.0);
        z.vel += acc * g_Dt;
        z.pos += z.vel * g_Dt;
        if (z.pos.y < ground + 0.03)
        {
            // 着地: その場に留まり、次の 1 秒で消える
            z.pos.y = ground + 0.03;
            z.vel = 0;
            z.age = max(z.age, z.life - 1.0);
        }
        g_Seeds[seedBase + k] = z;
    }
}
