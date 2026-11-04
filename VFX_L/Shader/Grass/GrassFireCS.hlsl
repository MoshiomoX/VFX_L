// ============================================================
// GrassFireCS.hlsl
// 草の延焼フィールドの 1 ステップ（World/GrassFireField、2026-10-12）。
// マップ全体の小区画ごとに 1 テクセル: x = 温度、y = 残りの燃料
// （1 = 生えたての草、0 = 燃え尽き）、z = 燃焼中は 1。踏みつけマップと
// 同じくピンポンで使う。
//
// 1 ステップの処理:
//   熱は風下へ運ばれ（セミラグランジュ: 風が来た元のテクセルを読む）、
//   隣へ広がり（拡散）、冷える。
//   燃料のある枯れ草のテクセル（g_Dry）は温度が g_Ignite を超えると
//   燃える: 熱を出し（自分を熱いまま保ち、隣に火をつける）、燃料を
//   使い切る。冷えた焼け跡はゆっくり生え戻り、g_Reburn まで戻って
//   初めてまた燃える: 燃料が少しでもあれば燃えるようにしていたら、前線が
//   燃やしたばかりの所へ戻ってきて永遠に燃え続けた（2026-11-01）。緑の
//   草は燃えないので、火は枯れ草の区画が終わる所で止まる。雨は冷却を
//   速め、燃料も早く燃え尽きさせる。
//   熱源 = 生きている GPU の範囲（爆発、メテオ、ファイアボールの命中）のうち
//   レシピに熱がある（g_RecipeHeat[vfxType] > 0、範囲プロファイルの
//   "heat" から）もの + CPU のスタンプ 1 つ（デバッグ / テスト用）。スレッドグループで
//   まず生きている熱い範囲を共有メモリに詰めるので、テクセルは
//   関係のある少数（たいてい 0〜5 個）だけを回る。
//   冷源（2026-11-01）: 負の熱、または凍らせる範囲。温度を 0 より
//   下げて火を消し、温まり直すまでは次の前線が越えてこないようにする。
//
// 陽的な拡散は g_Diffuse * dt <= 0.25（テクセル単位）の間は安定
// ============================================================

// GrassFireField::FireCB と一致させること
cbuffer GrassFireCB : register(b0)
{
    float2 g_MapOrigin;     // マップの最小の角のワールド xz
    float2 g_MapSize;       // m
    float2 g_WindDir;       // 単位 xz、風の吹いていく向き
    float g_WindSpeed;      // m/s（天気の強さ込み）
    float g_Dt;             // このステップの秒数（一時停止中は 0）
    float g_Ignite;         // 草が燃え始める温度
    float g_Cool;           // 1/s
    float g_Diffuse;        // texel^2/s
    float g_Downwind;       // 風速のうち熱を運ぶ割合（1 = 全部）
    float g_BurnRate;       // 燃焼中に毎秒減る燃料（1 / 燃える時間）
    float g_Release;        // 燃えているテクセルが毎秒自分に足す温度
    float g_Regrow;         // 冷えているときに毎秒戻る燃料
    float g_Rain;           // 0..1 の雨（天気）
    float3 g_Stamp;         // CPU からの着火: xz、半径（m）
    float g_StampHeat;      // 0 = スタンプなし
    float g_SourceMul;      // 範囲の半径 x これ = 熱する半径
    float g_Texel;          // 1 テクセルの大きさ（m）
    float g_RainCool;       // 雨 1 のときの追加の冷却
    float g_Reburn;         // 焼け跡はこの量まで燃料が戻ってからでないと燃えない
};

// = SwarmArea（Shader/Common/SwarmCommon.hlsli）、48 バイト。読むのは center / radius / vfxType だけ
struct FireArea
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

Texture2D<float4> g_In : register(t0);          // 前ステップ（温度、燃料、燃焼中）
Texture2D<float4> g_Ground : register(t1);      // GrassRenderer の地面テクスチャ、a = 草が生える所
StructuredBuffer<FireArea> g_Areas : register(t2);
Buffer<uint> g_AreaStates : register(t3);       // 0 = 死んでいる
StructuredBuffer<float> g_RecipeHeat : register(t4);   // エフェクトのレシピごと: 着火の熱（0 = 火ではない、< 0 = 冷たい）
Texture2D<float> g_Dry : register(t5);          // 1 = 枯れ草（GrassFireField::BuildDryMask）。燃えるのはこの草だけ
RWTexture2D<float4> g_Out : register(u0);

#define FIRE_MAX_AREAS 256u   // = SWARM_MAX_AREAS
#define FIRE_MAX_SOURCES 64u
#define FIRE_FREEZE_MASK 0xF0u   // = Swarm::kAreaFreezeShift、4 ビット（SwarmAreaFreeze）
#define FIRE_COLD_AREA (-1.5)    // 自分の熱を持たない凍らせる範囲
#define FIRE_MIN_TEMP (-2.0)

groupshared float4 gs_Source[FIRE_MAX_SOURCES];   // xz、半径、熱
groupshared uint gs_SourceCount;

float LoadT(int2 p, int2 maxP)
{
    return g_In.Load(int3(clamp(p, int2(0, 0), maxP), 0)).x;
}

// テクセル空間の位置でのバイリニアな温度（テクセル中心は .5）
float SampleT(float2 pos, int2 maxP)
{
    float2 q = pos - 0.5;
    int2 i = int2(floor(q));
    float2 f = q - float2(i);
    float a = LoadT(i, maxP), b = LoadT(i + int2(1, 0), maxP);
    float c = LoadT(i + int2(0, 1), maxP), d = LoadT(i + int2(1, 1), maxP);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    // ---- 生きている熱い範囲を共有メモリに詰める（64 スレッド、1 スレッド 4 範囲） ----
    if (gi == 0u)
        gs_SourceCount = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint heatCount, heatStride;
    g_RecipeHeat.GetDimensions(heatCount, heatStride);
    for (uint a = gi; a < FIRE_MAX_AREAS; a += 64u)
    {
        if (g_AreaStates[a] == 0u)
            continue;
        FireArea ar = g_Areas[a];
        float heat = (ar.vfxType < heatCount) ? g_RecipeHeat[ar.vfxType] : 0.0;
        if ((ar.flags & FIRE_FREEZE_MASK) != 0u)
            heat = min(heat, FIRE_COLD_AREA);   // アイススパイクの列は CPU から来る（レシピなし）
        if (heat == 0.0)
            continue;
        uint slot;
        InterlockedAdd(gs_SourceCount, 1u, slot);
        if (slot < FIRE_MAX_SOURCES)
            gs_Source[slot] = float4(ar.center.xz, ar.radius * g_SourceMul, heat);
    }
    GroupMemoryBarrierWithGroupSync();

    uint w, h;
    g_Out.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    const int2 p = int2(id.xy);
    const int2 maxP = int2(w - 1, h - 1);
    const float3 c = g_In.Load(int3(p, 0)).xyz;
    const float2 xz = g_MapOrigin + (float2(p) + 0.5) / float2(w, h) * g_MapSize;

    // ここに草があるか（地面テクスチャは解像度が別）
    uint gw, gh;
    g_Ground.GetDimensions(gw, gh);
    const float2 uv = (float2(p) + 0.5) / float2(w, h);
    const bool grass = g_Ground.Load(int3(int2(uv * float2(gw, gh)), 0)).a > 0.5
        && g_Dry.Load(int3(p, 0)) > 0.5;

    // ---- 熱: 風下へ運び、広げ、冷ます ----
    const float adv = min(g_Downwind * g_WindSpeed * g_Dt / max(g_Texel, 1e-3), 0.9);   // このステップで進むテクセル数
    float T = SampleT(float2(p) + 0.5 - g_WindDir * adv, maxP);
    const float lap = LoadT(p + int2(1, 0), maxP) + LoadT(p - int2(1, 0), maxP)
                    + LoadT(p + int2(0, 1), maxP) + LoadT(p - int2(0, 1), maxP) - 4.0 * c.x;
    T += g_Diffuse * lap * g_Dt;
    T -= T * g_Cool * (1.0 + g_RainCool * g_Rain) * g_Dt;

    // ---- 燃料: 熱いうちは燃え、冷えると生え戻る ----
    float fuel = c.y;
    const bool burning = grass && fuel > 0.001 && T > g_Ignite && (c.z > 0.5 || fuel >= g_Reburn);
    if (burning)
    {
        T += g_Release * g_Dt;
        fuel -= g_BurnRate * (1.0 + 2.0 * g_Rain) * g_Dt;
    }
    else if (grass && T < g_Ignite * 0.5)
    {
        fuel += g_Regrow * g_Dt;
    }

    // ---- 熱源: 爆発など（半径の内側は熱く、すぐ外側で減衰） ----
    const uint n = min(gs_SourceCount, FIRE_MAX_SOURCES);
    for (uint i = 0u; i < n; ++i)
    {
        float4 s = gs_Source[i];
        float d = distance(xz, s.xy);
        if (s.w < 0.0)
        {
            if (d < s.z * 1.15)
                T = min(T, s.w);
        }
        else if (d < s.z * 1.3)
            T = max(T, s.w * saturate(1.3 - d / max(s.z, 1e-3)));
    }
    if (g_StampHeat > 0.0)
    {
        float d = distance(xz, g_Stamp.xy);
        if (d < g_Stamp.z)
            T = max(T, g_StampHeat);
    }

    g_Out[p] = float4(clamp(T, FIRE_MIN_TEMP, 2.0), saturate(fuel), (burning && fuel > 0.001) ? 1.0 : 0.0, 0.0);
}
