// ============================================================
// WandComponent.h
// 杖。出力源（spells）と AOE（areas）の集約結果を持つ。
// spells / areas はグリッドの集約で毎回作り直される。ImGui からは読むだけ。
//
// ※マナは持たない。使い手の ManaComponent が持つ。
//   杖に持たせると「杖が2本 = 蓝が2本」になり、成長の書き先も割れる。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <vector>
#include "SpellID.h"
#include "Component/AreaStats.h"

// 1本の杖に対する発射の仕方（デバッグ用の切替）
enum class CastMode
{
    Auto,        // 射程内に標的がいれば撃つ（本番の挙動）
    Manual,      // 入力があった時だけ撃つ。照準は自動のまま
    DebugBurst,  // castInterval を無視して撃ち続ける（マナは消費する）
};
struct SpellStats
{
    ItemID id = ItemID::Fireball;

    // --- 分裂（空間展開: 同フレームに複数発）---
    int   projectileCount = 1;      // 一度に出る数
    float spreadAngle = 0.0f;   // 扇の角度（度）。count>1 の時だけ意味を持つ

    // --- 二重詠唱（時間展開: 少し遅れてもう一度）---
    int   castCount = 1;            // 1回の詠唱で撃つ回数
    float castDelay = 0.12f;        // 次の一発までの間隔（秒）

    // --- リズム / コスト ---
    float castInterval = 0.5f;      // 発動間隔（秒）。連発の間は進まない
    float manaCost = 10.0f;     // 1発あたりの消費（castCount 倍かかる）

    // --- 投射物の性能 ---
    // 基礎値は ProjectileProfile（profile 番）から写す。機能ルーンはその上に掛かる
    int   profile = 0;              // ProjectileProfileDB の番号（0 = 組み込みの直進）
    float damage = 10.0f;
    float speed = 20.0f;
    float radius = 0.25f;
    float lifetime = 3.0f;
    // 命中・着弾で出す範囲（プロファイルの hitArea）の威力に掛ける倍率。
    // 範囲の威力は GPU の範囲定義が持つので、弾ごとに渡す（能力アップ「魔法威力」。ItemInfo::ApplySpellPower）
    float areaDamageMul = 1.0f;

    // --- 誘発（基本魔法 → 上級魔法。集約が決める）---
    bool     triggered = false;   // 上級魔法。自分では撃たず、自分を triggerMask に持つ弾が消えた所で撃つ
    uint32_t triggerMask = 0;     // 基本魔法: この弾が消えたら誘発できる上級魔法（bit 0〜15 = spells の添字、bit 16〜31 = areas の添字（光線））

    // --- 実行時状態（WeaponSystem が更新）---
    float castTimer = 0.0f;      // 次に撃てるまでの残り
    int   pendingCasts = 0;         // 連発の残り回数
    float delayTimer = 0.0f;      // 次の連発までの残り
    DirectX::SimpleMath::Vector3 triggerPos;   // 上級魔法: 連発（二重詠唱）の続きを撃つ場所
};

struct WandComponent
{
    float range = 15.0f;
    DirectX::SimpleMath::Vector3 muzzleOffset = { 0.0f, 0.5f, 0.0f };

    CastMode castMode = CastMode::Auto;
    bool     castRequested = false;

    // プレイヤーが詠唱を止めている（Q / パッド Y で切替）。
    // 止めている間は新しい詠唱をしない = 魔力を使わずに溜められる。
    // 撃ち始めた連発（二重詠唱の残り）は最後まで撃ち切る。
    // 集約（BackpackAggregateSystem）は spells / areas しか作り直さないので、バックパックを組み替えても残る
    bool     castingPaused = false;
    float castAnimTimer = 0.0f;
    float castAnimDuration = 0.3f;
    // --- 集約結果（グリッドが真値。ここは派生データ）---
    std::vector<SpellStats> spells;   // 飛行物型
    std::vector<AreaStats>  areas;    // AOE 型
};
