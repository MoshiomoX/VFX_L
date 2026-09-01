// ============================================================
// SpellbookComponent.h
// 魔法使いが習得した魔法と、所持している設置枠。
//
// グリッドに置いてあるものとは別の概念：
//   ここは「持っている総数」、グリッドは「今組んでいる構成」。
//   使える数 = 所持数 - グリッド上に置いてある数
//
// 使用中の数をここに持たない理由：
//   2ヶ所で数を管理すると必ずずれる。
//   グリッドを走査すれば正確な数が出るので、真実は1ヶ所に保つ。
//   これにより「グリッドから外す = 手元に戻る」が自動で成立し、
//   移し替えの処理が一切要らなくなる。
// ============================================================
#pragma once
#include "SpellID.h"
#include <vector>

struct SpellbookComponent
{
    struct Entry
    {
        ItemID id;
        int    count = 1;   // 所持総数。同じ魔法を複数持てる
    };

    std::vector<Entry> entries;

    // 所持数（未習得なら 0）
    int GetCount(ItemID id) const
    {
        for (const auto& e : entries)
            if (e.id == id) return e.count;
        return 0;
    }

    bool HasLearned(ItemID id) const { return GetCount(id) > 0; }

    // 習得する。既に持っていれば数を増やす。
    // レベルアップの三択で同じ魔法を引いた時は、強化ではなく所持数が増える。
    // 同じ魔法を2つ並べて互いに影響させる構築が成立するため。
    void Learn(ItemID id, int n = 1)
    {
        for (auto& e : entries)
        {
            if (e.id == id) { e.count += n; return; }
        }
        entries.push_back({ id, n });
    }

    // 習得を取り消す（デバッグ用）。0 になった項目は消す。
    void Forget(ItemID id, int n = 1)
    {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].id != id) continue;

            entries[i].count -= n;
            if (entries[i].count <= 0)
                entries.erase(entries.begin() + i);
            return;
        }
    }

    void Clear() { entries.clear(); }
};