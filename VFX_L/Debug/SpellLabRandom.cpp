// ============================================================
// SpellLabRandom.cpp
// 実験場：バックパックをランダムに組む（2026-10-14 ユーザー：ランダムに魔法を置く選択肢）。説明は SpellLab.h
//   ・置いてある物を全部木箱へ戻してから、選んだ中身（基本魔法だけ / 魔法 + ルーン / 全部）から count 個を引いて
//     空いている場所にランダムな位置・向きで置く。上級魔法を引いた時は前提の基本魔法も一緒に引く
//   ・置く順番は 上級魔法 → その前提 → 他の基本魔法 → ルーン・召喚物。置き場所は「繋がる」所を優先する：
//     前提 = 上級魔法を発動させる側に入る所、ルーン = 誰かの影響マスに届く所、水晶玉 = 何かを貯蔵する所。無ければどこでも
//   ・置いた分は魔法書にも足す（所持数 ≥ 置いた数を保つ。バックパックから外せば木箱に落ちる）
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "Debug/SpellLab.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemTypes.h"

namespace
{
    constexpr int kGrid = BackpackComponent::GRID;

    bool IsUpperSpell(ItemID id)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        return c && !c->triggeredBy.empty();
    }

    // index の物が他の物と「繋がっている」か（置き場所の優先に使う）
    //   上級魔法 = 前提が揃って発動する / 基本魔法 = どれかの上級魔法を発動させる側に入っている /
    //   ルーン = 誰かの影響に届いている / 召喚物 = 何かを貯蔵している
    bool Connected(const BackpackComponent& bp, int index)
    {
        const ItemID id = bp.items[index].id;
        const ItemCategory cat = ItemDatabase::GetCategory(id);
        if (IsUpperSpell(id)) return BackpackLogic::IsTriggerReady(bp, index);
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            if (i == index) continue;
            if (cat == ItemCategory::Function)
            {
                const std::vector<int> inf = BackpackLogic::GetInfluencers(bp, i);
                if (std::find(inf.begin(), inf.end(), index) != inf.end()) return true;
            }
            else if (cat == ItemCategory::Summon)
            {
                if (BackpackLogic::StoredBy(bp, i) == index) return true;
            }
            else
            {
                // 基本魔法：上級魔法 i を発動させる側に入り、かつ同じ魔法がもう i に届いていない（2 個目を同じ所に置かない）
                const std::vector<int> drv = BackpackLogic::GetTriggerDrivers(bp, i);
                if (std::find(drv.begin(), drv.end(), index) == drv.end()) continue;
                bool redundant = false;
                for (int d : drv) if (d != index && bp.items[d].id == id) redundant = true;
                if (!redundant) return true;
            }
        }
        return false;
    }

    // 置く順番：上級魔法 → その前提の基本魔法 → 他の基本魔法 → ルーン・召喚物
    //（前提は上級魔法の隣に、ルーンは魔法の隣に置けるように、相手を先に置く）
    int PlaceRank(ItemID id, const std::vector<ItemID>& prereqs)
    {
        const ItemCategory c = ItemDatabase::GetCategory(id);
        if (c == ItemCategory::Function || c == ItemCategory::Summon) return 3;
        if (IsUpperSpell(id)) return 0;
        return (std::find(prereqs.begin(), prereqs.end(), id) != prereqs.end()) ? 1 : 2;
    }
}

// 置いてある魔法・ルーン・召喚物を全部外す（枠は残す）。外した物は所持数のまま = 木箱へ戻る
int SpellLab::ClearBackpack(Registry& reg, Entity player)
{
    auto& bp = reg.Get<BackpackComponent>(player);
    const int n = (int)bp.items.size();
    for (int i = n - 1; i >= 0; --i) BackpackLogic::Remove(bp, i);
    bp.dirty = true;
    return n;
}

std::wstring SpellLab::RandomBuild(Registry& reg, Entity player, int mode, int count)
{
    auto& bp = reg.Get<BackpackComponent>(player);
    auto& book = reg.Get<SpellbookComponent>(player);
    ClearBackpack(reg, player);

    // 引く候補
    std::vector<ItemID> pool;
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCategory c = ItemDatabase::GetCategory(id);
        const bool spell = (c == ItemCategory::Projectile || c == ItemCategory::Area);
        if (spell && !IsUpperSpell(id)) pool.push_back(id);
        else if (mode >= 1 && c == ItemCategory::Function) pool.push_back(id);
        else if (mode >= 2 && ((spell && IsUpperSpell(id)) || c == ItemCategory::Summon)) pool.push_back(id);
    }
    if (pool.empty()) return L"置ける物が無い";

    // 引く。上級魔法を引いたら前提の基本魔法（まだ無ければ）も入れる
    std::vector<ItemID> picks, prereqs;
    std::uniform_int_distribution<int> pick(0, (int)pool.size() - 1);
    for (int guard = 0; (int)picks.size() < count && guard < count * 8; ++guard)
    {
        const ItemID id = pool[pick(m_Rng)];
        if (IsUpperSpell(id))
        {
            if (std::find(picks.begin(), picks.end(), id) != picks.end()) continue;   // 同じ上級魔法は 1 つまで
            // 前提は上級魔法ごとに 1 つずつ要る（2 つの上級魔法が同じ前提を使うなら 2 個置く。1 個では片方しか発動しない）
            for (ItemID pre : ItemDatabase::GetCommon(id)->triggeredBy)
            {
                prereqs.push_back(pre);
                if (std::count(picks.begin(), picks.end(), pre) < std::count(prereqs.begin(), prereqs.end(), pre)) picks.push_back(pre);
            }
        }
        picks.push_back(id);
    }
    // 置く順番に並べ替える（同じ順位の中は引いた順のまま）
    std::stable_sort(picks.begin(), picks.end(),
        [&](ItemID a, ItemID b) { return PlaceRank(a, prereqs) < PlaceRank(b, prereqs); });

    // 置く。候補の場所を全部集めて、繋がる所があればその中から、無ければ全部の中からランダムに
    struct Spot { int r, c, rot; };
    std::vector<Spot> all, good;
    std::wstring names;
    int placed = 0;
    for (ItemID id : picks)
    {
        all.clear();
        good.clear();
        for (int rot = 0; rot < 4; ++rot)
            for (int r = 0; r < kGrid; ++r)
                for (int c = 0; c < kGrid; ++c)
                {
                    if (!BackpackLogic::CanPlace(bp, id, r, c, rot)) continue;
                    all.push_back({ r, c, rot });
                    const int idx = BackpackLogic::Place(bp, id, r, c, rot);
                    if (idx >= 0)
                    {
                        if (Connected(bp, idx)) good.push_back({ r, c, rot });
                        BackpackLogic::Remove(bp, idx);
                    }
                }
        if (all.empty()) continue;   // もう入らない
        const std::vector<Spot>& from = good.empty() ? all : good;
        const Spot s = from[std::uniform_int_distribution<int>(0, (int)from.size() - 1)(m_Rng)];
        if (BackpackLogic::Place(bp, id, s.r, s.c, s.rot) < 0) continue;
        book.Learn(id, 1);
        ++placed;
        if (placed <= 6)
        {
            const ItemCommon* ic = ItemDatabase::GetCommon(id);
            if (!names.empty()) names += L"、";
            names += ic ? ic->displayName : L"?";
        }
    }
    if (placed > 6) names += L" …";
    bp.dirty = true;

    if (placed == 0) return L"バックパックに空きが無い（枠を敷いてあるか確認）";
    return L"ランダムに " + std::to_wstring(placed) + L" 個置いた：" + names;
}
