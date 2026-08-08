// ============================================================
// BackpackAggregateSystem.h
// バックパックの配置から杖の出力（spells / areas）を算出する。
//
//  これがバックパックと戦闘を繋ぐ唯一の経路。
//   配置が変わった時（dirty）だけ再計算し、戦闘中は結果を読むだけ。
//
// 影響判定の方向：
//   「あるブロックの影響格」が「別のブロックの占位格」に重なったら影響成立。
//   大類型による制限は掛けない（将来「機能→機能」「攻撃→攻撃」も可能）。
// ============================================================
#pragma once
#include "Entity.h"
#include <vector>
#include <string>

class Registry;

class BackpackAggregateSystem
{
public:
    // dirty なバックパックを持つ Entity の杖を再構築する
    void Update(Registry& reg);

    // 強制的に再計算する（ImGui から呼ぶ用）
    void ForceRebuild(Registry& reg, Entity e);

    // ---- デバッグ表示用：直近の集約内容 ----
    struct AggregateLog
    {
        std::string sourceName;                // 攻撃ブロック名
        int         row = 0, col = 0;
        std::vector<std::string> influencedBy; // 影響を与えたブロック名
    };
    const std::vector<AggregateLog>& GetLog() const { return m_Log; }
    int GetRebuildCount() const { return m_RebuildCount; }

private:
    void Rebuild(Registry& reg, Entity e);

    std::vector<AggregateLog> m_Log;
    int m_RebuildCount = 0;
};