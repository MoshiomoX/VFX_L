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
    GoldenArrow,      // 黄金の矢：曲がらずまっすぐ飛ぶ速い矢（GoldenArrow プロファイル、直進）
    StoneShot,        // 石弾：山なりに飛ぶ重い岩（StoneShot プロファイル）。火球と組むとメテオを誘発する基礎魔法
    Poison,           // 毒：一番近い敵の足元へ山なりに投げ、減速 + 持続ダメージの池を残す基礎魔法（Poison プロファイル、Lob 型。2026-10-01）
    Beam,             // 魔導光線：高級の範囲魔法（2026-09-30）。追尾弾と弧の両方に隣り合うと目覚め、どちらかの弾が消えた方向へ手から光線を撃つ（Beam プロファイル、胶囊型）

    // --- 修飾符（隣接する出力源を強化。リストには入らない）---
    SplitRune,        // 分裂：一度の発射数 +1、ダメージ分散
    DoubleCastRune,   // 二重釈放：発射回数 +1、マナ倍
    Magnifier,        // 拡大鏡：斜め 4 マスの弾・範囲を 1.5 倍に（見た目も）、マナ 1.3 倍
    HasteRune,        // 加速：左右 2 マスの魔法の発動間隔 x0.75

    // ---- 設置枠 ----
    Frame3x3,
    Frame2x2,
    FrameLine3,
    FrameL,

    // ---- 能力値（レベルアップの候補にだけ出る。背包・呪文書には入らない）----
    MaxHealthUp,      // 生命の上限 +
    MaxManaUp,        // 魔力の上限 +
    MoveSpeedUp,      // 移動速度 +%
    JumpPowerUp,      // 跳躍力（跳ぶ初速）+%
    ManaRegenUp,      // 魔力回復 +%
    JumpCountUp,      // 跳躍回数 +1（空中の追加ジャンプ。回を重ねるごとに半分）
};

// 出力源かどうか（集約時の振り分け用）
inline bool IsSpellSource(ItemID id)
{
    return id == ItemID::Fireball || id == ItemID::Lightning
        || id == ItemID::ArcBolt || id == ItemID::HomingBolt || id == ItemID::Meteor
        || id == ItemID::GoldenArrow || id == ItemID::StoneShot || id == ItemID::Poison;
}
