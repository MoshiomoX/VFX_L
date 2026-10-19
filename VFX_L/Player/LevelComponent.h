// ============================================================
// LevelComponent.h
// 経験値とレベル、そして未選択の習得候補。
//
// PlayerStatsComponent と分ける理由：
//   あちらは「今の能力値」（移動速度・体格）。
//   こちらは「成長の進行状態」で、特に
//   「抽選済みだがまだ選ばれていない候補」という一時的な状態を持つ。
//   能力値の構造体に混ぜると意味が濁る。
//
// 曲線を式ではなく係数で持つ理由：
//   バランス調整は数字をいじる作業なので、
//   コードを書き換えずに ImGui から触れるようにしておく。
// ============================================================
#pragma once
#include "SpellID.h"
#include <vector>

struct LevelComponent
{
    // ---- 進行状態 ----
    int   level = 1;
    float experience = 0.0f;      // 現在のレベル内で溜まった量

    // ---- 曲線 ----
    // 必要量 = expBase + expPerLevel × n + expPerLevelSq × n²（n = level - 1）。
    // Megabonk 風に序盤は速く（1 → 2 は雑魚 3 体）、後半ほどゆっくり。
    // level 1 → 30、5 → 114、10 → 287、20 → 856。10 分で 30 前後の想定。
    // 指数だと後半が急に伸びて、1 周 10 分の想定と噛み合わない。
    float expBase = 30.0f;
    float expPerLevel = 15.0f;
    float expPerLevelSq = 1.5f;

    float ExpToNext() const
    {
        const float n = (float)(level - 1);
        return expBase + expPerLevel * n + expPerLevelSq * n * n;
    }

    float Progress() const
    {
        const float need = ExpToNext();
        return (need > 0.0f) ? (experience / need) : 0.0f;
    }

    bool CanLevelUp() const { return experience >= ExpToNext(); }

    // ---- 未選択の習得候補 ----
    // レベルアップで抽選され、プレイヤーが選ぶまで残る。
    // 空でなければ選択待ちの状態。
    std::vector<ItemID> pendingChoices;

    bool IsChoosing() const { return !pendingChoices.empty(); }

    // 今の四択で金貨を払って選び直した回数（2026-10-04。値段が回数で上がる。選んだら 0 に戻る）
    int rerollCount = 0;

    // 四択の画面を出すまでの残り秒（2026-10-15 ユーザー：「レベルアップが急すぎる」）。
    // レベルアップで候補を引いた時に LevelUpSystem::levelUpDelay が入り、その間はゲームを止めず
    // 「レベルアップ！」の案内だけ出す（GameUI が減らす）。報酬の箱の四択は 0（すぐ出す）
    float choiceDelay = 0.0f;

    // 今の四択が報酬の箱から出た物か（2026-10-18 ユーザー：「箱から出ても見出しがレベルアップになっている」→ 見出しを変える）
    bool choiceFromCrate = false;

    // 連続レベルアップは1回ずつ処理する。
    // まとめて上げると三択が何度も連続で出て、選ぶ作業だけが続く。
    // 余った経験値は持ち越して、選び終わってから次のレベルへ進む。
    void ConsumeLevelUp()
    {
        experience -= ExpToNext();
        if (experience < 0.0f) experience = 0.0f;
        ++level;
    }

    void ClearChoices() { pendingChoices.clear(); rerollCount = 0; choiceDelay = 0.0f; choiceFromCrate = false; }
};