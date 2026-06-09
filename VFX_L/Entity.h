#pragma once
// ============================================================
// Entity.h
// Entity = index（下位ビット）+ generation（上位ビット）
// generation で「古い Entity ハンドル」を検出し、ID 再利用の誤操作を防ぐ
// ============================================================
#pragma once
#include <cstdint>

// Entity は単なる 32bit 整数
// 下位 20bit = index（最大 1,048,575 Entity）
// 上位 12bit = generation（再利用検出用、4095 まで回って一周）
using Entity = uint32_t;

namespace EntityTraits
{
    // ビット配分
    constexpr uint32_t INDEX_BITS = 20;
    constexpr uint32_t GENERATION_BITS = 12;

    // マスク
    constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;          // 下位20bit
    constexpr uint32_t GENERATION_MASK = (1u << GENERATION_BITS) - 1; // 12bit分

    // 無効な Entity を表す番兵値
    constexpr Entity NULL_ENTITY = INDEX_MASK;  // index 部分が全ビット1

    // index 部分を取り出す
    inline uint32_t GetIndex(Entity e)
    {
        return e & INDEX_MASK;
    }

    // generation 部分を取り出す
    inline uint32_t GetGeneration(Entity e)
    {
        return (e >> INDEX_BITS) & GENERATION_MASK;
    }

    // index と generation から Entity を合成
    inline Entity Make(uint32_t index, uint32_t generation)
    {
        return (index & INDEX_MASK) | ((generation & GENERATION_MASK) << INDEX_BITS);
    }
}