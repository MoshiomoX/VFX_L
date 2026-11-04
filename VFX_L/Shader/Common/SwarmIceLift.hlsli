// ============================================================
// SwarmIceLift.hlsli
// 凍った池（World/WaterSurface、2026-11-01）: 氷は水面の高さにあり、敵が歩く
// 池底より 0.35 m 上なので、足が氷にめり込んでいた。
// 持ち上げるのは描画だけ（体、HP バー、丸影）で、
// 当たり判定のカプセルは池底のまま。
// SwarmCommon.hlsli とその bomber cbuffer の宣言（g_IceOn と、同じマップである
// 延焼マップの origin / size）の後、2 つのテクスチャレジスタを define した
// 後に include する。IceLift を呼ぶ前に g_IceOn は呼び出し側で確認する。
// ============================================================
#ifndef SWARM_ICE_LIFT_HLSLI
#define SWARM_ICE_LIFT_HLSLI

Texture2D<float> g_IceMap : register(SWARM_ICE_MAP_REG);    // 1 = 凍ったばかり .. 0 = 水面
Texture2D<float> g_PondTop : register(SWARM_POND_TOP_REG);  // 水面の高さ。池の外は地面よりずっと低い値

// xz に立ち、足が footY にある体をどれだけ持ち上げるか（氷の外は 0。氷の縁でなめらかに立ち上がる）
float IceLift(float2 xz, float footY)
{
    uint w, h;
    g_PondTop.GetDimensions(w, h);
    float2 st = (xz - float2(g_FireOriginX, g_FireOriginZ)) / float2(g_FireSizeX, g_FireSizeZ) * float2(w, h) - 0.5;
    int2 maxP = int2(w, h) - int2(1, 1);
    int2 c = clamp(int2(floor(st + 0.5)), int2(0, 0), maxP);
    float lift = g_PondTop.Load(int3(c, 0)) - footY;
    if (lift > 0.0)
    {
        int2 i = int2(floor(st));
        float2 f = st - float2(i);
        float a = g_IceMap.Load(int3(clamp(i, int2(0, 0), maxP), 0));
        float b = g_IceMap.Load(int3(clamp(i + int2(1, 0), int2(0, 0), maxP), 0));
        float d = g_IceMap.Load(int3(clamp(i + int2(0, 1), int2(0, 0), maxP), 0));
        float e = g_IceMap.Load(int3(clamp(i + int2(1, 1), int2(0, 0), maxP), 0));
        lift *= smoothstep(0.0, 0.05, lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y));
    }
    return max(lift, 0.0);
}

#endif
