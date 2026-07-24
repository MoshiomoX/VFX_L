// ============================================================
// SpellID.h
// アイテム種別。バックパックに置くのは全部これ。
// 出力源は spells リストに入り、修飾符は集約時にだけ使われる。
// ============================================================
#pragma once

enum class ItemID
{
    // --- 出力源（spells リストに入る）---
    Fireball = 0,
    Lightning,

    // --- 修飾符（隣接する出力源を強化。リストには入らない）---
    SplitRune,        // 分裂：一度の発射数 +1、ダメージ分散
    DoubleCastRune,   // 二重釈放：発射回数 +1、マナ倍
};

// 出力源かどうか（集約時の振り分け用）
inline bool IsSpellSource(ItemID id)
{
    return id == ItemID::Fireball || id == ItemID::Lightning;
}