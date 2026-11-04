// ============================================================
// WaterIceCS.hlsl
// 池の氷（World/WaterSurface、2026-11-01）。マップ全体を 0.8 m の
// テクセルで覆い、1 つにつき値 1 つ：1 = 凍った直後、g_IceLife 秒かけて
// 0 まで下がる。生きた冷たい範囲は、覆った池のテクセルを凍らせる：
// heat が負のレシピ（アイスランスの命中）か、凍結を持つ範囲すべて
// （アイススパイクの列。CPU から来るのでレシピが無い）。
// 水の無いテクセルは 0 のまま。延焼の冷源（GrassFireCS）と同じ規則。
// WaterRippleCS は氷を壁として扱い、WaterPS が描き、WaterStirCS は
// その上を歩く敵を遅くする。
// ============================================================

// WaterSurface::IceCB と一致させること
cbuffer WaterIceCB : register(b0)
{
    float2 g_MapOrigin;     // マップの最小の角のワールド xz
    float2 g_MapSize;       // m
    float g_Dt;             // このフレームの秒数（ポーズ中は 0）
    float g_IceLife;        // 凍ったテクセルが残る秒数
    float2 _icePad;
};

// = SwarmArea（Shader/Common/SwarmCommon.hlsli）、48 バイト
struct IceArea
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

Texture2D<float> g_Mask : register(t0);          // 1 = 池（波紋の場の 1024^2 テクセル）
StructuredBuffer<IceArea> g_Areas : register(t1);
Buffer<uint> g_AreaStates : register(t2);        // 0 = 死んでいる
StructuredBuffer<float> g_RecipeHeat : register(t3);   // エフェクトのレシピごと（GrassFireField）、< 0 = 冷たい
RWTexture2D<float> g_Ice : register(u0);

#define ICE_MAX_AREAS 256u       // = SWARM_MAX_AREAS
#define ICE_MAX_SOURCES 32u
#define ICE_MASK_SCALE 4         // 氷のテクセル 1 つあたりのマスクのテクセル数
#define ICE_FREEZE_MASK 0xF0u    // = Swarm::kAreaFreezeShift、4 ビット（SwarmAreaFreeze）
#define ICE_MIN_RADIUS 1.2       // アイスランスの命中は 0.5 m しかないが、それでも見える大きさの氷にする

groupshared float3 gs_Source[ICE_MAX_SOURCES];   // xz、半径
groupshared uint gs_SourceCount;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    if (gi == 0u)
        gs_SourceCount = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint heatCount, heatStride;
    g_RecipeHeat.GetDimensions(heatCount, heatStride);
    for (uint a = gi; a < ICE_MAX_AREAS; a += 64u)
    {
        if (g_AreaStates[a] == 0u)
            continue;
        IceArea ar = g_Areas[a];
        bool cold = (ar.flags & ICE_FREEZE_MASK) != 0u
            || (ar.vfxType < heatCount && g_RecipeHeat[ar.vfxType] < 0.0);
        if (!cold)
            continue;
        uint slot;
        InterlockedAdd(gs_SourceCount, 1u, slot);
        if (slot < ICE_MAX_SOURCES)
            gs_Source[slot] = float3(ar.center.xz, max(ar.radius, ICE_MIN_RADIUS));
    }
    GroupMemoryBarrierWithGroupSync();

    uint w, h;
    g_Ice.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;
    const int2 p = int2(id.xy);
    if (g_Mask.Load(int3(p * ICE_MASK_SCALE + ICE_MASK_SCALE / 2, 0)) < 0.5)
    {
        g_Ice[p] = 0.0;
        return;
    }

    float v = max(g_Ice[p] - g_Dt / max(g_IceLife, 0.1), 0.0);
    const float2 xz = g_MapOrigin + (float2(p) + 0.5) / float2(w, h) * g_MapSize;
    const uint n = min(gs_SourceCount, ICE_MAX_SOURCES);
    for (uint i = 0u; i < n; ++i)
        if (distance(xz, gs_Source[i].xy) < gs_Source[i].z)
            v = 1.0;
    g_Ice[p] = v;
}
