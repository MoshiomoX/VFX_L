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
};

// 直近のプレイの戦績
inline RunResult g_LastRun;

// ============================================================
// 面をまたぐ引き継ぎ（2026-09-30）。
// Boss を倒した面の終わりに戦闘シーンが書き、リザルトの「次のステージへ」で次の戦闘シーンが読む。
//   stage     : 次に始める面（1..StageConfig::kStageCount）。タイトルから始める時は 1
//   hasPlayer : true なら下の部品をプレイヤーへ写す（バックパック・魔法書・等級・能力値。HP / MP は満タンから）
// 戦闘シーンが読んだら hasPlayer を落とす（F5 の再読込で二重に効かないように）
// ============================================================
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Component/ManaComponent.h"
#include "Component/HealthComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/LevelComponent.h"
#include "Player/WalletComponent.h"

struct RunCarry
{
    int  stage = 1;
    bool hasPlayer = false;
    BackpackComponent    backpack;
    SpellbookComponent   spellbook;
    LevelComponent       level;
    WalletComponent      wallet;   
    ManaComponent        mana;
    HealthComponent      health;
    PlayerStatsComponent stats;
    uint32_t killsBefore = 0;     // 前の面までの撃破（リザルトの表示用）
    void Reset() { *this = RunCarry{}; }
};
inline RunCarry g_RunCarry;
