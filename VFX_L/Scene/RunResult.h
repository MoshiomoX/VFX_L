// ============================================================
// RunResult.h
// 1 回のプレイの戦績。ゲームシーンが死亡時に書き、リザルトシーンが読む。
//
// シーンは切替のたびに作り直されるので、シーンのメンバには置けない。
// 受け渡しはこのグローバル 1 つだけに限定する（SceneManager に
// 汎用の「引数」を足すほどの物ではない）。
// ============================================================
#pragma once
#include <cstdint>
#include "Component/BackpackComponent.h"

struct RunResult
{
    bool     valid = false;       // false ならまだ 1 回も遊んでいない
    float    survivedSec = 0.0f;  // 生存時間（秒）
    int      level = 1;           // 到達レベル
    uint32_t kills = 0;           // 撃破数（GPU counter の累計）
    float    expGained = 0.0f;    // 拾った経験値の合計
    bool     cleared = false;     // Boss を倒して面をクリアした（false = 力尽きた・途中でやめた）
    int      stage = 1;           // 遊んだ面（1..StageConfig::kStageCount）
    bool     hasNextStage = false;   // クリアして次の面がある（リザルトで「次のステージへ」を出す）
    int      pos = 1;             // このプレイの何面目か（1 始まり。2026-10-26 から同じマップを続けて戦う）
    int      tier = 1;            // Tier = 何面続けるか（ステージ選択。2026-10-21）
    // ---- リザルトの作り直し（2026-10-09）で足した物 ----
    int      goldEarned = 0;      // この面で拾った金貨
    int      cratesOpened = 0;    // この面で開けた報酬の箱
    BackpackComponent backpack;   // 終わった時のバックパック（リザルトに並べて見せる）
};

// 直近のプレイの戦績
inline RunResult g_LastRun;

// ============================================================
// 面をまたぐ引き継ぎ（2026-09-30）。
// Boss を倒した面の終わりに戦闘シーンが書き、リザルトの「次のステージへ」で次の戦闘シーンが読む。
//   stage     : マップ（1..StageConfig::kStageCount）。プレイの間ずっと同じ。pos = 次に始めるのが何面目か
//   hasPlayer : true なら下の部品をプレイヤーへ写す（バックパック・魔法書・等級・能力値。HP / MP は満タンから）
// 戦闘シーンが読んだら hasPlayer を落とす（F5 の再読込で二重に効かないように）
// ============================================================
#include "Component/SpellbookComponent.h"
#include "Component/ManaComponent.h"
#include "Component/HealthComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/LevelComponent.h"
#include "Player/WalletComponent.h"
#include "Player/ShieldComponent.h"

struct RunCarry
{
    int  stage = 1;   // マップ。プレイの間ずっと同じ（2026-10-26 ユーザー「三つのマップを完全に分ける」：森林 → 森林 2 → 森林 3 と同じマップを続ける）
    // プレイの計画（ステージ選択で決める。2026-10-21）：tier 面続けて戦う。pos = 次に始めるのが何面目か（1 始まり、最後 = tier）。
    // クリアしたか力尽きたかに関わらず、リザルトから選択画面へ戻るまで持つ
    int  pos = 1;
    int  tier = 1;
    bool hasPlayer = false;
    BackpackComponent    backpack;
    SpellbookComponent   spellbook;
    LevelComponent       level;
    WalletComponent      wallet;   
    ManaComponent        mana;
    HealthComponent      health;
    ShieldComponent      shield;   // 上限と戻りの速さ（面の初めは満タン）
    PlayerStatsComponent stats;
    uint32_t killsBefore = 0;     // 前の面までの撃破（リザルトの表示用）
    void Reset() { *this = RunCarry{}; }
    // 計画（マップ・何面目・Tier）だけ残して捨てる
    void ResetKeepPlan() { const int s = stage, p = pos, t = tier; Reset(); stage = s; pos = p; tier = t; }
};
inline RunCarry g_RunCarry;
