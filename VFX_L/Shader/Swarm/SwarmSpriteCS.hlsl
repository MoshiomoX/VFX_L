// ============================================================
// SwarmSpriteCS.hlsl
// GPU 範囲のスプライトシートアニメ。1 フレームに 2 パス
// （SwarmSystem::DispatchSprites）、どちらも範囲の発射パスの後:
//   g_SpriteMode 0（経過）: プールのスロット 1 つにつき 1 スレッド。age += dt、
//                           寿命が尽きたインスタンスを片付ける。
//   g_SpriteMode 1（生成）: 範囲のスロット 1 つにつき 1 スレッド。スロットが
//                           生きていて、前フレームに生きていなかったか
//                           timeLeft が増えた（範囲は減る一方なので、使い回された
//                           スロットは大きい値になる）なら新しい範囲。その範囲の
//                           レシピの Sprite エントリそれぞれに、リングプールの
//                           インスタンスを 1 つ割り当てる
//                           （満杯なら一番古いものを上書き）。
// areaSeen には前フレームの asuint(timeLeft) を残す。0xFFFFFFFF = 死亡 / 未確認。
// CPU が生成した範囲は vfxType 0（エフェクトは CPU が再生）で、
// レシピ 0 はスプライトを持たないので、ここでは何も始まらない。
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
        float scale = SwarmAreaScale(a);   // 拡大鏡: スプライト全体を範囲と一緒に拡大
        SwarmSprite s;
        s.position = a.center + spriteDefs[def].offset * scale;
        s.age = 0.0;
        s.def = def;
        s.alive = 1u;
        s.sizeScale = scale;
        s._pad = 0u;
        sprites[slot] = s;
    }
}
