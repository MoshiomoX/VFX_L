// ============================================================
// SpellID.h
// アイテム種別。バックパックに置くのは全部これ。
// 出力源は spells リストに入り、修飾符は集約時にだけ使われる。
// ============================================================
#pragma once

enum class ItemID
{
	Unknown = 0,
    // --- 出力源（spells リストに入る）---
    Fireball ,
    Lightning,
    ArcBolt,          // 弧（ArcOnce プロファイル）
    HomingBolt,       // 全追尾（HomingFull）
    Meteor,           // 空から落ちて着弾点で爆発（Meteor プロファイル、Drop 型）

    // --- 修飾符（隣接する出力源を強化。リストには入らない）---
    SplitRune,        // 分裂：一度の発射数 +1、ダメージ分散
    DoubleCastRune,   // 二重釈放：発射回数 +1、マナ倍

    // ---- 設置枠 ----
    Frame3x3,
    Frame2x2,
    FrameLine3,
    FrameL,

    // ---- 能力値（レベルアップの候補にだけ出る。背包・呪文書には入らない）----
    MaxHealthUp,      // 生命の上限 +
    MaxManaUp,        // 魔力の上限 +
};

// 出力源かどうか（集約時の振り分け用）
inline bool IsSpellSource(ItemID id)
{
    return id == ItemID::Fireball || id == ItemID::Lightning
        || id == ItemID::ArcBolt || id == ItemID::HomingBolt || id == ItemID::Meteor;
}