// ============================================================
// SandFieldCS.hlsl
// 砂漠の砂面を 1 ステップ進める（World/SandField、2026-11-02）。
// フィールド全体の 2 枚のマップ。草の延焼と同じくピンポン：
//   state (float4): x = 温度、y = 乾き具合（1 = 乾いている、0 = びしょ濡れ）、
//                   z = 熱く乾いた砂が揺らめく所で 1（陽炎）、
//                   w = 0..1 風が乾いた砂を巻き上げる強さ（2026-11-03）
//   track (float) : 砂面の深さ（負 = くぼみ）
// 1 ステップごと：
//   温度は隣へ広がり、冷える。
//   濡れた砂は乾いていき、熱いほど速い（火は水たまりを乾かす）。
//   くぼみは風下へ運ばれ（風が低い所を風上側から
//   埋める）、ぼやけて、ゆっくり 0 に戻る。
//   発生源 = 生きている GPU 範囲：レシピ表がエフェクトのレシピごとに (heat, wet) を
//   与える。凍結の範囲（アイススパイクの列は CPU から出るのでレシピが
//   ない）は濡らして冷やす。爆発（SWARM_AREA_SHAKE）はクレーターを残す。
//   プレイヤーの足は足跡を押す。雑魚の足は SandTrackCS が担当。
//   風：局所の風（風源 + ワールドの風の一部）が
//   g_LiftWind より速い所では乾いた砂が舞う：w がその強さ。そこでは
//   くぼみが速く埋まり、GrassFireEmitCS（channel 1）がそのテクセルから
//   砂粒と低い砂霞を舞い上げる。池は舞わない。
// 陽的な拡散は k * dt <= 0.25（テクセル単位）の間は安定
// ============================================================

#include "../Common/WindSources.hlsli"

// SandField::SandCB（144 バイト）と一致させること
cbuffer SandCB : register(b0)
{
    float2 g_MapOrigin;
    float2 g_MapSize;
    float2 g_Wind;          // m/s, xz
    float g_Dt;
    float g_Texel;          // 1 テクセルあたりのメートル
    float g_HeatDiffuse;    // texel^2/s
    float g_Cool;           // 1/s
    float g_DryRate;        // 温度 0 での 1 秒あたりの乾き
    float g_HeatDry;        // 温度 1 あたりの追加の乾き
    float g_HazeHeat;       // 温度がこれより高く（かつ乾いて）いれば陽炎
    float g_TrackFill;      // 1/s：くぼみが戻る速さ
    float g_TrackBlur;      // texel^2/s
    float g_TrackWind;      // くぼみを運ぶのに使う風速の割合
    float3 g_Player;        // プレイヤーの足（ワールド）
    float g_PlayerOn;       // 1 = このステップで足跡を押す
    float g_PlayerRadius;   // m
    float g_PlayerDepth;    // m（負）
    float g_CraterDepth;    // m（正。クレーターの中心は -この値）
    float g_SourceMul;      // 範囲の半径 x この値 = 影響する半径
    float4 g_Stamp;         // CPU のスタンプ：xz, 半径, 熱（パネル / 自動テスト用）
    float g_StampWet;       // 0..1：スタンプで砂も濡らす
    float g_LiftWind;       // 乾いた砂が舞い始める風速（m/s）
    float g_LiftRange;      // それを何 m/s 超えたら最大の強さで舞うか
    float g_WorldLift;      // ワールドの風が与える舞い上げの割合（風源は全部）
    float g_TrackWindFill;  // 舞う砂はくぼみをこの倍率で速く埋める
    float3 _pad;
};

// = SwarmArea（Shader/Common/SwarmCommon.hlsli）、48 バイト
struct SandArea
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

Texture2D<float4> g_In : register(t0);
Texture2D<float> g_InTrack : register(t1);
StructuredBuffer<SandArea> g_Areas : register(t2);
Buffer<uint> g_AreaStates : register(t3);
StructuredBuffer<float2> g_RecipeSand : register(t4);   // エフェクトのレシピごと：(heat, wet)
StructuredBuffer<WindSource> g_WindSources : register(t5);   // 竜巻 / 渦 / 突風（SwarmWindSourceCS）
Texture2D<float> g_Water : register(t6);                 // 1 = 池（乾かない、舞わない）
RWTexture2D<float4> g_Out : register(u0);
RWTexture2D<float> g_OutTrack : register(u1);

#define SAND_MAX_AREAS 256u     // = SWARM_MAX_AREAS
#define SAND_MAX_SOURCES 64u
#define SAND_FREEZE_MASK 0xF0u  // = Swarm::kAreaFreezeShift、4 ビット
#define SAND_SHAKE 8u           // = SWARM_AREA_SHAKE（爆発）

groupshared float4 gs_Src[SAND_MAX_SOURCES];    // xz, 半径, 熱
groupshared float2 gs_Src2[SAND_MAX_SOURCES];   // wet, クレーター（1 = あり）
groupshared uint gs_Count;

float LoadTrack(int2 p, int2 maxP)
{
    return g_InTrack.Load(int3(clamp(p, int2(0, 0), maxP), 0));
}

float LoadTemp(int2 p, int2 maxP)
{
    return g_In.Load(int3(clamp(p, int2(0, 0), maxP), 0)).x;
}

// テクセル空間の位置で双線形補間した深さ（テクセル中心は .5）
float SampleTrack(float2 pos, int2 maxP)
{
    float2 q = pos - 0.5;
    int2 i = int2(floor(q));
    float2 f = q - float2(i);
    float a = LoadTrack(i, maxP), b = LoadTrack(i + int2(1, 0), maxP);
    float c = LoadTrack(i + int2(0, 1), maxP), d = LoadTrack(i + int2(1, 1), maxP);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    // ---- 砂に影響する生きている範囲を詰める ----
    if (gi == 0u)
        gs_Count = 0u;
    GroupMemoryBarrierWithGroupSync();
    uint tableCount, tableStride;
    g_RecipeSand.GetDimensions(tableCount, tableStride);
    for (uint a = gi; a < SAND_MAX_AREAS; a += 64u)
    {
        if (g_AreaStates[a] == 0u)
            continue;
        SandArea ar = g_Areas[a];
        float2 hw = (ar.vfxType < tableCount) ? g_RecipeSand[ar.vfxType] : float2(0.0, 0.0);
        if ((ar.flags & SAND_FREEZE_MASK) != 0u)
            hw = float2(min(hw.x, -1.0), max(hw.y, 0.8));   // 氷は溶けて砂に染みる
        float crater = ((ar.flags & SAND_SHAKE) != 0u) ? 1.0 : 0.0;
        if (hw.x == 0.0 && hw.y == 0.0 && crater == 0.0)
            continue;
        uint slot;
        InterlockedAdd(gs_Count, 1u, slot);
        if (slot < SAND_MAX_SOURCES)
        {
            gs_Src[slot] = float4(ar.center.xz, ar.radius * g_SourceMul, hw.x);
            gs_Src2[slot] = float2(hw.y, crater);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint w, h;
    g_Out.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;
    const int2 p = int2(id.xy);
    const int2 maxP = int2(w - 1, h - 1);
    const float4 c = g_In.Load(int3(p, 0));
    const float2 xz = g_MapOrigin + (float2(p) + 0.5) / float2(w, h) * g_MapSize;

    // ---- 温度：広がる、冷える ----
    float T = c.x;
    const float lapT = LoadTemp(p + int2(1, 0), maxP) + LoadTemp(p - int2(1, 0), maxP)
                     + LoadTemp(p + int2(0, 1), maxP) + LoadTemp(p - int2(0, 1), maxP) - 4.0 * T;
    T += g_HeatDiffuse * lapT * g_Dt;
    T -= T * g_Cool * g_Dt;

    // ---- 乾き：乾いていく、熱いほど速い ----
    float dry = c.y + g_DryRate * (1.0 + g_HeatDry * max(T, 0.0)) * g_Dt;

    // ---- くぼみ：風下へ運ばれ、ぼやけ、埋まっていく ----
    const float2 adv = g_Wind * (g_TrackWind * g_Dt / max(g_Texel, 1e-3));   // このステップで動くテクセル数
    float d = SampleTrack(float2(p) + 0.5 - clamp(adv, -0.9, 0.9), maxP);
    const float lapD = LoadTrack(p + int2(1, 0), maxP) + LoadTrack(p - int2(1, 0), maxP)
                     + LoadTrack(p + int2(0, 1), maxP) + LoadTrack(p - int2(0, 1), maxP) - 4.0 * LoadTrack(p, maxP);
    d += g_TrackBlur * lapD * g_Dt;
    d -= d * g_TrackFill * g_Dt;

    // ---- 発生源 ----
    const uint n = min(gs_Count, SAND_MAX_SOURCES);
    for (uint i = 0u; i < n; ++i)
    {
        float4 s = gs_Src[i];
        float2 s2 = gs_Src2[i];
        float r = distance(xz, s.xy) / max(s.z, 1e-3);
        if (r >= 1.2)
            continue;
        float k = saturate(1.2 - r);
        if (s.w > 0.0)
            T = max(T, s.w * k);
        else if (s.w < 0.0)
            T = min(T, s.w * k);
        if (s2.x > 0.0)
            dry = min(dry, 1.0 - s2.x * saturate((1.0 - r) * 3.0));
        if (s2.y > 0.5 && r < 1.0)
            d = min(d, -g_CraterDepth * (1.0 - r * r));   // min：数フレーム生きる範囲でもそれ以上深く掘らない
    }
    if (g_Stamp.z > 0.0)
    {
        float r = distance(xz, g_Stamp.xy) / g_Stamp.z;
        if (r < 1.0)
        {
            if (g_Stamp.w != 0.0)
                T = (g_Stamp.w > 0.0) ? max(T, g_Stamp.w) : min(T, g_Stamp.w);
            if (g_StampWet > 0.0)
                dry = min(dry, 1.0 - g_StampWet);
        }
    }
    if (g_PlayerOn > 0.5)
    {
        float r = distance(xz, g_Player.xz) / max(g_PlayerRadius, 1e-3);
        if (r < 1.0)
            d = min(d, g_PlayerDepth * (1.0 - r * r));
    }

    // ---- 風：乾いた砂が舞い（w = 強さ）、舞う砂がくぼみを埋める ----
    float srcLift;
    float3 srcAir = WindSourceAir(g_WindSources, xz, srcLift);
    float rangeInv = 1.0 / max(g_LiftRange, 0.1);
    float blowSrc = saturate((length(srcAir.xz) - g_LiftWind) * rangeInv);
    float blowWorld = saturate((length(g_Wind) - g_LiftWind) * rangeInv) * g_WorldLift;
    // 風源はたいていワールドの風に重なって吹くので、強い方を採る
    float blow = max(blowSrc, blowWorld);

    T = clamp(T, -2.0, 2.0);
    dry = saturate(dry);
    float water = g_Water.Load(int3(p, 0));
    if (water > 0.5)
        dry = 0.0;
    float lift = blow * dry * dry;   // 湿った砂はほとんど動かず、濡れた砂はまったく動かない
    d -= d * g_TrackFill * g_TrackWindFill * lift * g_Dt;
    float haze = (T > g_HazeHeat && dry > 0.3) ? 1.0 : 0.0;
    g_Out[p] = float4(T, dry, haze, lift);
    g_OutTrack[p] = clamp(d, -1.0, 0.0);
}
