// ============================================================
// GameProgress.h
// プレイヤーの進み具合（2026-10-21 ユーザー：「ステージ選択を Megabonk に完全に倣う」）。
//   Megabonk の構造 = マップ × Tier。マップ（草原 / 砂漠 / 遺跡）を選び、Tier n = そのマップを n ステージ続けて戦う
//   （2026-10-26 ユーザー「三つのマップを完全に分ける」：森林 → 森林 2 → 森林 3。最後のステージの Boss を倒せばクリア、Tier 1 = 1 面だけ）。
//   解放は段階的：同じマップの Tier n をクリアすると Tier n+1。砂漠は草原の Tier 2 をクリアすると、遺跡は砂漠の Tier 2 をクリアすると解放
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

    bool mapUnlocked[kMaps] = { true, false, false };   // [map - 1]（前のマップの Tier 2 クリアで開く。UnlockAll / 保存の互換のため残す）
    bool tierCleared[kMaps][kTiers] = {};               // [map - 1][tier - 1]：そのマップの Tier をクリアした
    int  lastMap = 1;    // 前回選んだ物（選択画面の初期カーソル。1 始まり）
    int  lastTier = 1;

    // そのマップで選べる最大の Tier（10-26 からどのマップも 3）
    static int MaxTier(int map) { return StageConfig::MaxTier(map); }

    bool IsMapUnlocked(int map) const;
    bool IsTierCleared(int map, int tier) const;
    // 選べる Tier：マップが開いていて、tier == 1 か、1 つ下の Tier をクリア済み
    bool IsTierOpen(int map, int tier) const;

    // 出来事。何か新しく開いたら true（画面の案内用）
    bool OnTierCleared(int map, int tier);       // そのマップの Tier を最後まで戦い抜いた（次の Tier、Tier 2 なら次のマップも開く）

    void UnlockAll();
    void ResetAll();

    bool Save() const;
    bool Load();
};
