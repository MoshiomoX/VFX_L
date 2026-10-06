// ============================================================
// SwarmSpriteCS.hlsl
// Sprite-sheet animations for GPU areas. Two passes per frame
// (SwarmSystem::DispatchSprites), both after the area emit pass:
//   g_SpriteMode 0 (age)  : one thread per pool slot. age += dt, retire
//                           instances whose life is over.
//   g_SpriteMode 1 (spawn): one thread per area slot. A slot holds a NEW
//                           area when it is alive and either was not seen
//                           alive last frame or its timeLeft went UP (an
//                           area only counts down, so a reused slot shows
//                           a larger value). Then every Sprite entry of the
//                           area's recipe gets an instance in the ring pool
//                           (oldest overwritten when full).
// areaSeen keeps asuint(timeLeft) of the last frame, 0xFFFFFFFF = dead/unseen.
// Areas spawned by the CPU carry vfxType 0 (the CPU plays their effect),
// recipe 0 has no sprites, so nothing starts for them here.
// ============================================================
#include "../Common/SwarmCommon.hlsli"
#include "../Common/SwarmSprite.hlsli"

cbuffer SpriteStepCB : register(b3)
{
    float g_SpriteDt;
    uint g_SpriteMode;
    uint g_SpritePoolSize;
    uint g_SpriteAreaCount;
};

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
StructuredBuffer<SwarmRecipe> recipes : register(t2);
StructuredBuffer<SwarmSpriteDef> spriteDefs : register(t3);

RWStructuredBuffer<SwarmSprite> sprites : register(u0);
RWStructuredBuffer<uint> areaSeen : register(u1);
RWByteAddressBuffer spriteHead : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;

    if (g_SpriteMode == 0u)
    {
        if (i >= g_SpritePoolSize)
            return;
        SwarmSprite s = sprites[i];
        if (s.alive == 0u)
            return;
        s.age += g_SpriteDt;
        if (s.age >= spriteDefs[s.def].life)
            s.alive = 0u;
        sprites[i] = s;
        return;
    }

    if (i >= g_SpriteAreaCount)
        return;
    if (areaStates[i] == SWARM_DEAD)
    {
        areaSeen[i] = 0xFFFFFFFFu;
        return;
    }

    SwarmArea a = areas[i];
    uint seen = areaSeen[i];
    bool fresh = (seen == 0xFFFFFFFFu) || (a.timeLeft > asfloat(seen) + 1e-4);
    areaSeen[i] = asuint(a.timeLeft);
    if (!fresh)
        return;

    SwarmRecipe r = recipes[a.vfxType];
    for (uint k = 0u; k < r.spriteCount; ++k)
    {
        uint slot;
        spriteHead.InterlockedAdd(0, 1u, slot);
        slot %= g_SpritePoolSize;

        uint def = r.spriteStart + k;
        float scale = SwarmAreaScale(a);   // Magnifier: offset follows the hit radius, the sprite grows with sqrt (see SwarmEmitCS)
        SwarmSprite s;
        s.position = a.center + spriteDefs[def].offset * scale;
        s.age = 0.0;
        s.def = def;
        s.alive = 1u;
        s.sizeScale = sqrt(scale);
        s._pad = 0u;
        sprites[slot] = s;
    }
}
