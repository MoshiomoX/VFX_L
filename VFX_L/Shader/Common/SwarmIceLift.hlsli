// ============================================================
// SwarmIceLift.hlsli
// Frozen ponds (World/WaterSurface, 2026-11-01): the ice lies at the water
// line, 0.35 m above the pond floor an enemy walks on, so its feet sank into
// the sheet. Only what is drawn is lifted (the body, its hp bar, its blob
// shadow); the hit capsule stays on the floor.
// Include after SwarmCommon.hlsli with its bomber cbuffer declared (g_IceOn
// and the fire map's origin / size, the same map), after defining the two
// texture registers. The caller checks g_IceOn before calling IceLift.
// ============================================================
#ifndef SWARM_ICE_LIFT_HLSLI
#define SWARM_ICE_LIFT_HLSLI

Texture2D<float> g_IceMap : register(SWARM_ICE_MAP_REG);    // 1 = just frozen .. 0 = open water
Texture2D<float> g_PondTop : register(SWARM_POND_TOP_REG);  // the water line, far below the ground outside a pond

// how far to lift a body standing at xz with its feet at footY (0 off the ice; eases in over the ice's edge)
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
