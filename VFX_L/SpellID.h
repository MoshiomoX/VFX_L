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
    Poison,           // 猛毒の沼：毒の瓶を投げて大きな毒沼を残す上級魔法（Poison プロファイル、Lob 型）
    Swamp,            // 泥沼：敵の足元へ山なりに投げ、落ちた時だけ飛沫のダメージ、残る沼は減速だけ（Swamp プロファイル、Lob 型）
    Spore,            // 胞子：毒の胞子の塊をゆっくり飛ばし、当たった所に 2 秒だけ全部毒素の霧を残す（Spore プロファイル、直進）
    VoidVortex,       // 渦：紫の渦をゆっくり飛ばし、当たった所で 2 秒開いて周りの敵を中心へ引き寄せる（VoidVortex プロファイル、直進）
    IceLance,         // アイスランス：まっすぐ飛ぶ氷の槍。当たった 1 体を凍らせる（IceLance プロファイル、直進）
    IceSpike,         // アイススパイク：氷のトゲを突き出して凍らせる上級魔法（IceSpike プロファイル、Drop 型）
    Beam,             // 魔導光線：手から光線を撃つ上級の範囲魔法（Beam プロファイル、カプセル型）
    Twister,          // 竜巻：竜巻を起こし、進みながら敵を巻き込む上級の範囲魔法（Twister プロファイル、Roam 型）
    Gust,             // 突風：プレイヤーの前の敵を吹き飛ばす + 少しのダメージ（Gust プロファイル、push）
    Detonate,         // 一斉起爆：空中の自分の弾を全部その場で爆発させる（Detonate プロファイル）
    SwordRing,        // 魔剣の輪：数秒ごとに 3 本の幻の剣を呼び、プレイヤーの周りを回して斬る（SwordRing プロファイル、Blades 型）
    SpinSlash,        // 回旋斬：幻の曲刀で自分の周りを一回り斬る（SpinSlash プロファイル、Blades 型）
    VampireBats,      // 吸血コウモリ：不規則に飛んで敵を噛み、戻って HP を返す蝙蝠を放つ（VampireBats プロファイル、Bats 型）
    // --- 出口魔法。ItemCommon::outletCells を持つ ---
    OutletSpark,      // 導きの火花：敵を追う小さな火花。当たった所で出口の魔法が発動する（OutletSpark プロファイル）
    OutletSplit,      // 分かれ玉：敵を貫いて飛ぶ玉。飛び切った所で 2 つの出口の魔法が同時に発動する（OutletSplit プロファイル）
    OutletSeed,       // 時限の種：足元へ投げる種。落ちた 2 秒後に出口の魔法が発動する（OutletSeed プロファイル、Lob 型）
    OutletMine,       // 浮遊機雷：ゆっくり漂い、敵に触れると爆発して出口の魔法が発動する（OutletMine プロファイル）
    OrbitGuard,       // 周回弾：プレイヤーの周りを回り、触れた敵を貫く（OrbitGuard プロファイル、Orbit 型）

    // --- 修飾ルーン（隣接する出力源を強化。リストには入らない）---
    SplitRune,        // 分裂：一度の発射数 +1、ダメージ分散
    DoubleCastRune,   // 二重詠唱：発射回数 +1、マナ倍
    Magnifier,        // 拡大鏡：斜め 4 マスの弾・範囲を 1.5 倍に（見た目も）、マナ 1.3 倍
    HasteRune,        // 加速：左右 2 マスの魔法の発動間隔 x0.75
    ChainRune,        // 連鎖：上下 2 マスの魔法の弾が敵に当たっても消えず、近くの別の敵へ 2 回まで跳ぶ。威力は少し下がる
    LarpaRune,        // 分身：両端の魔法の弾が消える時、周りへ 6 発の分身を放つ

    // --- 召喚物 ---
    CrystalBall,      // 水晶玉：左右 2 マスの魔法（上級魔法も可）を貯蔵し、プレイヤーの周りに出る光球が代わりに撃つ。ルーンの影響は受けない

    // ---- 設置枠（Items/Frames.h）----
    Frame3x3,         // 拡張枠 3x3（初期装備。札では珍しい）
    Frame2x3,         // 拡張枠 2x2（名前は昔の形のまま）
    FrameL,           // 拡張枠 1x2（名前は昔の形のまま）
    FrameCross,       // 拡張枠 1x1（名前は昔の形のまま）
    FrameLine4,       // 拡張枠 一列（4 マス）
    FrameArea,        // 範囲の枠（2x2）：乗っている魔法の当たり判定・範囲 x1.2
    FrameFocus,       // 一撃の枠（1x3）：乗っている魔法の単体への威力 x1.25
    FrameHaste,       // 速射の枠（1x2）：乗っている魔法の発動間隔 x0.85
    FrameLifesteal,   // 吸血の枠（2x2）：乗っている魔法が与えたダメージの 3% が HP に戻る

    // ---- 能力値（レベルアップの候補にだけ出る。バックパック・呪文書には入らない）----
    MaxHealthUp,      // 生命の上限 +
    MaxManaUp,        // 魔力の上限 +
    MoveSpeedUp,      // 移動速度 +%
    JumpPowerUp,      // 跳躍力（跳ぶ初速）+%
    ManaRegenUp,      // 魔力回復 +%
    JumpCountUp,      // 跳躍回数 +1（空中の追加ジャンプ。回を重ねるごとに半分）
    SpellPowerUp,     // 魔法威力 +%（全部の攻撃魔法のダメージ）
    ShieldUp,         // シールドの上限 +（HP より先に受け、5 秒被弾しなければ戻る）
};

// 出力源かどうか（集約時の振り分け用）
inline bool IsSpellSource(ItemID id)
{
    return id == ItemID::Fireball || id == ItemID::Lightning
        || id == ItemID::ArcBolt || id == ItemID::HomingBolt || id == ItemID::Meteor
        || id == ItemID::GoldenArrow || id == ItemID::StoneShot || id == ItemID::Poison
        || id == ItemID::IceLance || id == ItemID::IceSpike || id == ItemID::Swamp || id == ItemID::Spore
        || id == ItemID::VoidVortex || id == ItemID::OutletSpark || id == ItemID::OutletSplit || id == ItemID::OutletSeed
        || id == ItemID::OutletMine || id == ItemID::OrbitGuard;
}
