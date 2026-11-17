// ============================================================
// SwarmRecipe.hlsli
// VFX レシピの構造体（SwarmRecipe）。
// ============================================================

#ifndef SWARM_RECIPE_HLSLI
#define SWARM_RECIPE_HLSLI

// ============================================================
// レシピ：vfxType の中身。32 バイト。
// 各範囲は種類ごとの表を指す。弾は複数のエミッタ（芯 + 尾）、
// メッシュ、ライトを持てる。それぞれを専用の CS が、
// 同じ「弾 1 つに 1 スレッド」の骨組みで処理する。
// SwarmVFXTable.h の Swarm::VFXRecipe と一致させること
// ============================================================
struct SwarmRecipe
{
    uint particleStart;
    uint particleCount;
    uint modelStart;
    uint modelCount;

    uint lightStart;
    uint lightCount;
    uint spriteStart; // SwarmSpriteDef の表（GPU の範囲だけ）
    uint spriteCount;
};

#endif
