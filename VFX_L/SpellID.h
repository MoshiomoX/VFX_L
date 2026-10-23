// ============================================================
// SpellID.h
// アイテム種別。バックパックに置くのは全部これ。
// 出力源は spells リストに入り、修飾ルーンは集約時にだけ使われる。
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
    StoneShot,        // 石弾：山なりに飛ぶ重い岩（StoneShot プロファイル）。火球と組むとメテオを誘発する基本魔法
    Poison,           // 猛毒の沼：上級魔法（2026-10-17 に基本魔法「毒」から格上げ）。泥沼に隣り合うと発動し、弾が消えた所へ毒の瓶を投げて大きな毒沼を残す（Poison プロファイル、Lob 型）
    Swamp,            // 泥沼：一番近い敵の足元へ山なりに投げ、落ちた時だけ飛沫のダメージ、残る沼は減速だけの基本魔法（Swamp プロファイル、Lob 型。2026-10-17）
    Spore,            // 胞子：毒の胞子の塊をゆっくり飛ばし、当たった所に 2 秒だけ全部毒素の霧を残す基本魔法（Spore プロファイル、直進）。泥沼と組んで猛毒の沼を発動させる（2026-10-17、茨の代わり）
    VoidVortex,       // 渦：紫の渦をゆっくり飛ばし、当たった所で 2 秒開いて周りの敵を中心へ引き寄せる基本魔法（VoidVortex プロファイル、直進。2026-10-22）
    IceLance,         // アイスランス：まっすぐ飛ぶ氷の槍。当たった 1 体を凍らせる基本魔法（IceLance プロファイル、直進。2026-10-08）
    IceSpike,         // アイススパイク：上級魔法（2026-10-10）。アイスランスとストーンショットの両方に隣り合うと目覚め、弾が消えた所から前へ氷のトゲを突き出して凍らせる（IceSpike プロファイル、Drop 型）
    Beam,             // 魔導光線：上級の範囲魔法（2026-09-30）。追尾弾と弧の両方に隣り合うと目覚め、どちらかの弾が消えた方向へ手から光線を撃つ（Beam プロファイル、カプセル型）
    Twister,          // 竜巻：上級の範囲魔法（2026-10-22）。渦と突風の両方に隣り合うと目覚め、渦の弾が消えた所・突風の吹いた所に竜巻を起こし、敵の方へ進みながら巻き込む（Twister プロファイル、Roam 型）
    Gust,             // 突風：基本の範囲魔法（2026-10-12）。プレイヤーの周りの敵を吹き飛ばす + 少しのダメージ（Gust プロファイル、push）

    // --- 修飾ルーン（隣接する出力源を強化。リストには入らない）---
    SplitRune,        // 分裂：一度の発射数 +1、ダメージ分散
    DoubleCastRune,   // 二重詠唱：発射回数 +1、マナ倍
    Magnifier,        // 拡大鏡：斜め 4 マスの弾・範囲を 1.5 倍に（見た目も）、マナ 1.3 倍
    HasteRune,        // 加速：左右 2 マスの魔法の発動間隔 x0.75
    ChainRune,        // 連鎖：上下 2 マスの魔法の弾が敵に当たっても消えず、近くの別の敵へ 2 回まで跳ぶ。威力は少し下がる（2026-10-16）

    // --- 召喚物（2026-10-06）---
    CrystalBall,      // 水晶玉：左右 2 マスの魔法（上級魔法も可）を貯蔵し、プレイヤーの周りに出る光球が代わりに撃つ。ルーンの影響は受けない

    // ---- 設置枠 ----
    Frame3x3,
    Frame2x2,
    FrameLine3,
    FrameL,

    // ---- 能力値（レベルアップの候補にだけ出る。バックパック・呪文書には入らない）----
    MaxHealthUp,      // 生命の上限 +
    MaxManaUp,        // 魔力の上限 +
    MoveSpeedUp,      // 移動速度 +%
    JumpPowerUp,      // 跳躍力（跳ぶ初速）+%
    ManaRegenUp,      // 魔力回復 +%
    JumpCountUp,      // 跳躍回数 +1（空中の追加ジャンプ。回を重ねるごとに半分）
    SpellPowerUp,     // 魔法威力 +%（全部の攻撃魔法のダメージ。2026-10-02）
    ShieldUp,         // シールドの上限 +（HP より先に受け、5 秒被弾しなければ戻る。2026-10-07）
};

// 出力源かどうか（集約時の振り分け用）
inline bool IsSpellSource(ItemID id)
{
    return id == ItemID::Fireball || id == ItemID::Lightning
        || id == ItemID::ArcBolt || id == ItemID::HomingBolt || id == ItemID::Meteor
        || id == ItemID::GoldenArrow || id == ItemID::StoneShot || id == ItemID::Poison
        || id == ItemID::IceLance || id == ItemID::IceSpike || id == ItemID::Swamp || id == ItemID::Spore
        || id == ItemID::VoidVortex;
}
