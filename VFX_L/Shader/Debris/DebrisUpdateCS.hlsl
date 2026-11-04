// ============================================================
// DebrisUpdateCS.hlsl
// 地面の落ち葉（World/GroundDebris、2026-10-13）の 1 ステップ。
// 1 枚に 1 スレッド：
//   1) 置き直し：その葉の格子セルが窓から出た（または g_Reset）
//      -> 新しいセルの地面に静止させて置く（セルのハッシュ
//      + 地形のシード：同じセルなら同じ葉）。崖の上と、
//      密度で間引かれる所では隠す。
//   2) 静止中：その場所の世界風（突風の波）が g_LiftWind を
//      超えたか、突風の扇か爆発があると舞い上がる（風は確率で
//      判定し、一面が一斉に跳ねないようにする）。
//   3) 空中：重力 + 空気の速度（世界風、突風の扇の流れ、
//      爆発の外向きの爆風）へ g_Drag で寄せる
//      -> 葉はゆっくり落ちて風を追う。空中では回転する。
//      高さマップに着地し、小さく 1 回跳ねてから静止する。
// ============================================================
#include "DebrisCommon.hlsli"
#include "../Common/WindSources.hlsli"

// = SwarmArea（Shader/Common/SwarmCommon.hlsli）、48 バイト。読むのは center / radius / flags だけ
struct DebrisArea
{
    float3 center;
    float radius;
    float damage;
    float timeLeft;
    float tickInterval;
    float tickTimer;
    float halfHeight;
    uint flags;
    uint vfxType;
    uint tickNow;
};

Texture2D<float> g_Height : register(t0);          // GridWorld の高さ（GrassRenderer のテクスチャ）
StructuredBuffer<DebrisArea> g_Areas : register(t1);
Buffer<uint> g_AreaStates : register(t2);          // 0 = 死んでいる
StructuredBuffer<float4> g_AreaEnds : register(t3); // スロットごとのカプセル（光線）の終点、AreaTickCS から
StructuredBuffer<WindSource> g_WindSources : register(t4);   // 局所風：砂旋（SwarmWindSourceCS、2026-10-19）
SamplerState s_Linear : register(s0);
#define DEBRIS_AREA_CAPSULE 4u   // SWARM_AREA_CAPSULE：光線。中心は杖口で、本体は線分
RWStructuredBuffer<Debris> g_Debris : register(u0);

float GroundAt(float2 xz)
{
    float2 uv = (xz - g_MapOrigin) / g_MapSize;
    return g_Height.SampleLevel(s_Linear, uv, 0);
}

// xz での世界風（GrassVS の突風の式。草の葉・パーティクルと同じ時計）
float2 WindAt(float2 xz)
{
    float phase = dot(xz, g_WindDir) * g_WindWaveScale * 6.2831853 - g_WindTime * g_WindWaveSpeed;
    float w = sin(phase) * 0.5 + 0.5;
    w *= w;
    return g_WindDir * (g_WindSpeed + g_WindGust * w);
}

// 突風の扇 / 円盤の中の空気の速度（外は 0）。どれかが xz を覆っていれば lift = 1
float3 GustAir(float2 xz, out float lift)
{
    float3 air = 0;
    lift = 0.0;
    for (uint k = 0u; k < g_GustCount; ++k)
    {
        float2 root = g_GustA[k].xy, dir = g_GustA[k].zw;
        float len = g_GustB[k].x, radius = g_GustB[k].y, strength = g_GustB[k].z;
        float2 d = xz - root;
        if (len <= 0.0)
        {
            float dist = length(d);
            if (dist < radius)
            {
                float2 out2 = dist > 1e-3 ? d / dist : float2(0, 1);
                air += float3(out2.x, 0.45, out2.y) * (g_GustSpeed * strength * (1.0 - 0.5 * dist / radius));
                lift = 1.0;
            }
            continue;
        }
        float along = dot(d, dir);
        float2 sideV = d - dir * along;
        float side = length(sideV);
        float t = saturate(along / len);
        float r = radius * lerp(0.35, 1.0, t);
        if (along > -0.4 && along < len && side < r)
        {
            float2 flow = dir;
            if (side > 1e-3) flow = normalize(dir + sideV / side * (0.35 * side / r));   // 少し広がる
            float s = strength * (1.0 - 0.6 * t) * (0.7 + 0.3 * (1.0 - side / r));       // 根元と中心線で最も強い
            air += float3(flow.x, 0.4, flow.y) * (g_GustSpeed * s);
            lift = 1.0;
        }
    }
    return air;
}

// このステップで生きている揺れる範囲を、範囲スロット 1 つにつき 1 ビットで持つ（main の先頭でグループごとに 1 回作る。2026-10-25：
// それ以前は葉 1 枚ごとに 256 個の範囲の状態を全部読んでいた）。BlastAccel は立っているビットをスロット順にたどるので、合計は
// 昔の全走査のループと同じ順に足される
#define DEBRIS_SHAKE_WORDS (DEBRIS_MAX_AREAS / 32u)
groupshared uint gs_ShakeMask[DEBRIS_SHAKE_WORDS];

// 生きている爆発の範囲（Swarm::kAreaShake）が与える外向きの加速度。どれかの中なら lift = 1
float3 BlastAccel(float3 pos, out float lift)
{
    float3 acc = 0;
    lift = 0.0;
    for (uint w = 0u; w < DEBRIS_SHAKE_WORDS; ++w)
    {
        uint bits = gs_ShakeMask[w];
        while (bits != 0u)
        {
            uint a = w * 32u + firstbitlow(bits);
            bits &= bits - 1u;
            DebrisArea ar = g_Areas[a];
            // 光線（2026-10-13）：半径 × 2.5 の帯の中で、線分から横へ押し出し、少し線分に沿っても押す。
            // 以前は中心 = 杖口の円盤として扱っていたので、葉が光線に沿ってではなく
            // プレイヤーの足元から弾け飛んでいた
            if ((ar.flags & DEBRIS_AREA_CAPSULE) != 0u)
            {
                float2 seg = g_AreaEnds[a].xz - ar.center.xz;
                float segLen = length(seg);
                if (segLen < 1e-3)
                    continue;
                float t = saturate(dot(pos.xz - ar.center.xz, seg) / (segLen * segLen));
                float2 d = pos.xz - (ar.center.xz + seg * t);
                float side = length(d);
                float r = ar.radius * 2.5;
                if (side > r || abs(pos.y - ar.center.y) > ar.halfHeight + 3.0)
                    continue;
                float2 out2 = side > 1e-3 ? d / side : float2(seg.y, -seg.x) / segLen;
                float f = g_BlastAccel * 0.5 * (1.0 - side / r);
                acc += float3(out2.x * f, f * 0.4, out2.y * f) + float3(seg.x, 0.0, seg.y) / segLen * (f * 0.3);
                lift = 1.0;
                continue;
            }
            float r = ar.radius * 1.5;
            float2 d = pos.xz - ar.center.xz;
            float dist = length(d);
            if (dist > r || abs(pos.y - ar.center.y) > ar.halfHeight + 3.0)
                continue;
            float2 out2 = dist > 1e-3 ? d / dist : float2(0, 1);
            float f = g_BlastAccel * (1.0 - dist / r);
            acc += float3(out2.x * f, f * 0.55, out2.y * f);
            lift = 1.0;
        }
    }
    return acc;
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    // ---- 生きている揺れる範囲をビットマスクにする（どの return よりも前に、グループの全スレッドが参加する） ----
    if (gi < DEBRIS_SHAKE_WORDS)
        gs_ShakeMask[gi] = 0u;
    GroupMemoryBarrierWithGroupSync();
    // DEBRIS_MAX_AREAS <= 256 = 1 スレッドに範囲 1 つ（ここをループにすると 1 周に展開されて X3557 が出る）
    if (gi < DEBRIS_MAX_AREAS)
    {
        if (g_AreaStates[gi] != 0u && (g_Areas[gi].flags & DEBRIS_AREA_SHAKE) != 0u)
            InterlockedOr(gs_ShakeMask[gi >> 5], 1u << (gi & 31u));
    }
    GroupMemoryBarrierWithGroupSync();

    uint i = id.x;
    if (i >= g_Side * g_Side)
        return;

    Debris d = g_Debris[i];

    // ---- 1) 今の窓の中での、この葉の格子セル（トーラス状に回り込む） ----
    int side = (int) g_Side;
    int ix = (int) (i % g_Side), iz = (int) (i / g_Side);
    int2 base = (int2) floor((g_CamPos.xz - g_Window) / g_Spacing);
    int kx = ix - base.x, kz = iz - base.y;
    int cx = base.x + (kx - (int) floor((float) kx / (float) side) * side);   // [base, base + side) に回り込ませる
    int cz = base.y + (kz - (int) floor((float) kz / (float) side) * side);
    uint cell = ((uint) cx & 0xFFFFu) | (((uint) cz & 0xFFFFu) << 16);

    if (cell != d.cell || g_Reset != 0u)
    {
        uint s = DebrisHash((uint) cx * 73856093u ^ (uint) cz * 19349663u ^ g_Seed);
        float2 xz = (float2(cx, cz) + float2(DebrisRand(s), DebrisRand(s))) * g_Spacing;
        float2 uv = (xz - g_MapOrigin) / g_MapSize;
        uint tw, th;
        g_Height.GetDimensions(tw, th);
        float2 du = float2(1.0 / tw, 0), dv = float2(0, 1.0 / th);
        float y = g_Height.SampleLevel(s_Linear, uv, 0);
        float hx = g_Height.SampleLevel(s_Linear, uv + du, 0) - g_Height.SampleLevel(s_Linear, uv - du, 0);
        float hz = g_Height.SampleLevel(s_Linear, uv + dv, 0) - g_Height.SampleLevel(s_Linear, uv - dv, 0);
        float slope = length(float2(hx, hz)) / (2.0 * g_MapSize.x / tw);
        bool inside = all(uv > 0.0) && all(uv < 1.0);

        d.pos = float3(xz.x, y + 0.015, xz.y);
        d.restT = 10.0;
        d.vel = 0;
        d.cell = cell;
        d.rot = float3((DebrisRand(s) - 0.5) * 0.5, DebrisRand(s) * 6.2831853, (DebrisRand(s) - 0.5) * 0.5);
        d.air = 0.0;
        d.angVel = 0;
        // 種類：葉 50%、草くず 35%、小枝 15%
        float kr = DebrisRand(s);
        uint kind = kr < 0.5 ? 0u : (kr < 0.85 ? 1u : 2u);
        d.seed = kind | ((s >> 8) & 0xFFFCu);
        if (!inside || slope > g_MaxSlope || DebrisRand(s) > g_Density)
            d.seed |= DEBRIS_HIDDEN;
        g_Debris[i] = d;
        return;
    }

    if ((d.seed & DEBRIS_HIDDEN) != 0u || g_Dt <= 0.0)
        return;

    uint rs = DebrisHash(d.seed ^ asuint(g_WindTime) ^ i * 2654435761u);
    float2 wind = WindAt(d.pos.xz);
    float gustLift, blastLift;
    float3 gustAir = GustAir(d.pos.xz, gustLift);
    float3 blast = BlastAccel(d.pos, blastLift);
    // 局所の風源（砂旋）：周りを渦巻く + 芯の近くの上昇気流
    float vortexLift;
    float3 vortexAir = WindSourceAir(g_WindSources, d.pos.xz, vortexLift);

    // ---- 2) 静止中：舞い上がるか？ ----
    if (d.air < 0.5)
    {
        d.restT += g_Dt;
        bool lift = false;
        float3 kick = 0;
        if (gustLift > 0.5)
        {
            lift = true;
            kick = gustAir * 0.35 + float3(0, 1.5 + 1.5 * DebrisRand(rs), 0);
        }
        else if (blastLift > 0.5)
        {
            lift = true;
            kick = blast * 0.08 + float3(0, 2.0 + 2.0 * DebrisRand(rs), 0);
        }
        else if (vortexLift > 0.12 && d.restT > 0.1)
        {
            // 竜巻が通り過ぎる：葉が 1 枚ずつはがれる（芯へ近いほど確率が上がる）
            if (DebrisRand(rs) < vortexLift * 4.0 * g_Dt)
            {
                lift = true;
                kick = vortexAir * 0.6 + float3(0, 0.8 + 1.2 * DebrisRand(rs), 0);
            }
        }
        else
        {
            // 天気の風：葉ごとの閾値（0.75 .. 1.25 倍）と毎秒の確率で、一面がさざ波のように動く
            float w = length(wind);
            float th = g_LiftWind * (0.75 + 0.5 * (float) ((d.seed >> 6) & 0xFFu) / 255.0);
            if (w > th && d.restT > 0.3)
            {
                float chance = saturate((w - th) / th) * 1.5 * g_Dt;
                if (DebrisRand(rs) < chance)
                {
                    lift = true;
                    kick = float3(wind.x, 0, wind.y) * 0.5 + float3(0, 0.5 + 0.9 * DebrisRand(rs), 0);
                }
            }
        }
        if (!lift)
        {
            // 静止中の風でのはためき：板が少し揺れる（位置は変えない）
            g_Debris[i] = d;
            return;
        }
        d.air = 1.0;
        d.restT = 0.0;
        d.vel = kick;
        float sp = max(length(kick), 1.0);
        d.angVel = float3(DebrisRand(rs) - 0.5, DebrisRand(rs) - 0.5, DebrisRand(rs) - 0.5) * (8.0 + 6.0 * sp);
        d.pos.y += 0.02;
    }

    // ---- 3) 空中 ----
    d.restT += g_Dt;
    float3 air = float3(wind.x, 0, wind.y) + gustAir + vortexAir;
    float3 acc = float3(0, -g_Gravity, 0) + blast + (air - d.vel) * g_Drag;
    d.vel += acc * g_Dt;
    d.pos += d.vel * g_Dt;
    d.rot += d.angVel * g_Dt;
    d.angVel *= 1.0 - saturate(0.6 * g_Dt);

    float ground = GroundAt(d.pos.xz);
    if (d.pos.y <= ground + 0.015 && d.vel.y <= 0.0)
    {
        d.pos.y = ground + 0.015;
        float sp = length(d.vel);
        if (sp > 2.0 && gustLift < 0.5 && blastLift < 0.5 && vortexLift < 0.3)
        {
            // 地面に沿って小さく 1 回跳ねる
            d.vel.y = -d.vel.y * 0.3;
            d.vel.xz *= 0.55;
            d.angVel *= 0.5;
        }
        else if (gustLift > 0.5 || blastLift > 0.5)
        {
            // まだ流れの中：地面に沿って滑り続ける
            d.vel.y = max(d.vel.y, 0.0);
        }
        else
        {
            d.vel = 0;
            d.air = 0.0;
            d.restT = 0.0;
            d.angVel = 0;
            // 平らに寝かせる（yaw は保つ）
            d.rot.x = (DebrisRand(rs) - 0.5) * 0.5;
            d.rot.z = (DebrisRand(rs) - 0.5) * 0.5;
        }
    }
    // マップの端から出た：少なくとも地面の上には保つ
    d.pos.y = max(d.pos.y, ground + 0.015);
    g_Debris[i] = d;
}
