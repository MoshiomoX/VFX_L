// ============================================================
// BackpackOutlet.cpp
// 出口（2026-10-31）：出口魔法の出口マスに誰が乗っているか。集約・説明カード・バックパックの表示で共用。
//
// 繋がりは輪にならない：分かれ玉を火花の出口に置くと、分かれ玉の左の出口が火花へ戻ることが普通に起きる。
// 輪を認めると起点が無くなって全部撃たなくなるので、items の順（先に置いた物が上流）に繋ぎ、
// 上流へ戻る出口は無かったことにする。バックパックではその出口は薄く出る
// ============================================================
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"

namespace
{
    // 出口マスに乗っている攻撃魔法（輪の判定の前）
    std::vector<int> RawTargets(const BackpackComponent& bp, int itemIndex)
    {
        std::vector<int> out;
        const PlacedItem& src = bp.items[itemIndex];
        const ItemCommon* c = ItemDatabase::GetCommon(src.id);
        if (!c || c->outletCells.empty()) return out;

        for (const CellOffset& off : BackpackLogic::RotateShape(c->outletCells, src.rotation))
        {
            const int r = src.row + off.row, col = src.col + off.col;
            if (r < 0 || r >= BackpackComponent::GRID || col < 0 || col >= BackpackComponent::GRID) continue;
            const int t = bp.GetOccupant(r, col);
            if (t < 0 || t == itemIndex || t >= (int)bp.items.size()) continue;
            if (!ItemDatabase::IsAttackType(bp.items[t].id)) continue;
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        }
        return out;
    }

    // from から出口を辿って to に着くか
    bool Reaches(const std::vector<std::vector<int>>& edges, int from, int to)
    {
        std::vector<int> stack = { from };
        std::vector<bool> visited(edges.size(), false);
        while (!stack.empty())
        {
            const int i = stack.back();
            stack.pop_back();
            if (i == to) return true;
            if (visited[i]) continue;
            visited[i] = true;
            stack.insert(stack.end(), edges[i].begin(), edges[i].end());
        }
        return false;
    }
}

namespace BackpackLogic
{
    std::vector<std::vector<int>> GetOutletEdges(const BackpackComponent& bp)
    {
        std::vector<std::vector<int>> edges(bp.items.size());
        for (int i = 0; i < (int)bp.items.size(); ++i)
            for (int t : RawTargets(bp, i))
                if (!Reaches(edges, t, i))
                    edges[i].push_back(t);
        return edges;
    }

    std::vector<int> GetOutletTargets(const BackpackComponent& bp, int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= (int)bp.items.size()) return {};
        return GetOutletEdges(bp)[itemIndex];
    }

    std::vector<int> GetOutletDrivers(const BackpackComponent& bp, int itemIndex)
    {
        std::vector<int> out;
        const auto edges = GetOutletEdges(bp);
        for (int i = 0; i < (int)edges.size(); ++i)
            if (std::find(edges[i].begin(), edges[i].end(), itemIndex) != edges[i].end())
                out.push_back(i);
        return out;
    }

    std::vector<OutletLine> SolveOutletLines(const std::vector<std::vector<int>>& edges, const std::vector<OutletNode>& nodes)
    {
        const size_t n = nodes.size();
        std::vector<OutletLine> lines(n);
        for (size_t i = 0; i < n; ++i)
        {
            lines[i].interval = nodes[i].interval;
            lines[i].mana = nodes[i].mana;
        }
        // 輪は無いので、魔法の数だけ回せば一番長い道の端まで届く
        for (size_t pass = 0; pass < n; ++pass)
        {
            std::vector<OutletLine> next(n);
            for (size_t i = 0; i < n; ++i)
            {
                const OutletNode& u = nodes[i];
                const int fan = (std::max)(u.fan, 1);
                const float rhythm = (u.baseInterval > 0.0f) ? u.interval / u.baseInterval : 1.0f;
                const float mul = lines[i].powerMul * std::pow(kOutletSplitDamageMul, (float)(fan - 1));
                float slowest = 0.0f, payload = 0.0f;
                for (int t : edges[i])
                {
                    slowest = (std::max)(slowest, lines[t].interval);
                    payload += (float)(std::max)(nodes[t].casts, 1) * lines[t].mana;
                    next[t].powerMul = (std::min)(next[t].powerMul, mul);
                }
                next[i].interval = (std::max)(u.interval, slowest * rhythm);
                next[i].mana = u.mana + (float)fan * payload;
            }
            lines.swap(next);
        }
        return lines;
    }
}
