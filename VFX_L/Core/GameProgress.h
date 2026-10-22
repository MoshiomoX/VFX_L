// ============================================================
// GameProgress.h
// プレイヤーの進み具合（2026-10-21 ユーザー：「ステージ選択を Megabonk に完全に倣う」）。
//   Megabonk の構造 = マップ × Tier。マップ（草原 / 砂漠 / 遺跡）を選び、Tier n = そのマップから n ステージ続けて戦う
//   （草原 → 砂漠 → 遺跡 の順。最後のステージの Boss を倒せばクリア、Tier 1 は門が無い = 1 面だけ）。
//   解放は段階的：Tier 1 をクリアすると Tier 2、Tier 2 をクリアすると Tier 3。
//   砂漠は「草原 Tier 2 で 2 面目に入った」時、遺跡は「3 面目（遺跡）に入った」時に開始マップとして解放
//   （Megabonk の CL4NK 条件のようなキャラ条件はこのゲームに無いので無し）。
//   Assets/Data/Progress.json（Res::Cfg::Progress）に保存。Debug の選択画面に「全部解放」「リセット」がある
// ============================================================
#pragma once
#include "World/StageConfig.h"

class GameProgress
{
public:
    static GameProgress& Get();

    static constexpr int kMaps = StageConfig::kStageCount;
    static constexpr int kTiers = StageConfig::kMaxTier;

    bool mapUnlocked[kMaps] = { true, false, false };   // [map - 1]
    bool tierCleared[kMaps][kTiers] = {};               // [map - 1][tier - 1]：そのマップからの Tier をクリアした
    int  lastMap = 1;    // 前回選んだ物（選択画面の初期カーソル。1 始まり）
    int  lastTier = 1;

    // そのマップから始めて選べる最大の Tier（= 残りの面数。遺跡は 1、砂漠は 2、草原は 3）
    static int MaxTier(int map) { return StageConfig::MaxTier(map); }

    bool IsMapUnlocked(int map) const;
    bool IsTierCleared(int map, int tier) const;
    // 選べる Tier：マップが開いていて、tier == 1 か、1 つ下の Tier をクリア済み
    bool IsTierOpen(int map, int tier) const;

    // 出来事。何か新しく開いたら true（画面の案内用）
    bool OnStageEntered(int stage);              // プレイの中でそのステージに入った → 開始マップとして解放
    bool OnTierCleared(int map, int tier);       // そのマップからの Tier を最後まで戦い抜いた

    void UnlockAll();
    void ResetAll();

    bool Save() const;
    bool Load();
};
