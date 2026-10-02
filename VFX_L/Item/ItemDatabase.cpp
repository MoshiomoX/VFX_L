// ============================================================
// ItemDatabase.cpp
// ============================================================
#include "Item/ItemDatabase.h"

// ---- 各アイテムの定義（増やす時はここに include を1行）----
#include "Item/Items/Fireball.h"
#include "Item/Items/ArcBolt.h"
#include "Item/Items/HomingBolt.h"
#include "Item/Items/GoldenArrow.h"
#include "Item/Items/Meteor.h"
#include "Item/Items/StoneShot.h"
#include "Item/Items/Poison.h"
#include "Item/Items/Beam.h"
#include "Item/Items/SplitRune.h"
#include "Item/Items/DoubleCastRune.h"
#include "Item/Items/Magnifier.h"
#include "Item/Items/HasteRune.h"
#include "Item/Items/Frame3x3.h"
#include "Item/Items/MaxHealthUp.h"
#include "Item/Items/MaxManaUp.h"
#include "Item/Items/MoveSpeedUp.h"
#include "Item/Items/JumpPowerUp.h"
#include "Item/Items/ManaRegenUp.h"
#include "Item/Items/JumpCountUp.h"
#include "Item/ItemDataFile.h"
#include <unordered_map>
#include <utility>
#include <iostream>

namespace
{
    // 種類ごとの登録所（実体はここだけが持つ）
    std::unordered_map<ItemID, ProjectileItemDef> g_Projectiles;
    std::unordered_map<ItemID, FunctionItemDef>   g_Functions;
    std::unordered_map<ItemID, AreaItemDef>       g_Areas;
    std::unordered_map<ItemID, FrameItemDef>      g_Frames;
    std::unordered_map<ItemID, StatItemDef>       g_Stats;

    std::vector<ItemID> g_AllIDs;          // 全ID（UI の一覧表示用）。能力値は入れない
    std::vector<ItemID> g_LevelUpOnlyIDs;  // レベルアップの候補にだけ出る物（能力値）
    bool g_Initialized = false;

    // Items/*.h に書いてある形（json で上書きする前）。編集器の「コードの既定に戻す」用
    std::unordered_map<ItemID,
        std::pair<std::vector<CellOffset>, std::vector<CellOffset>>> g_CodeShapes;

    // 形の差し替え用。外へは const でしか出さない
    ItemCommon* MutableCommon(ItemID id)
    {
        if (auto it = g_Projectiles.find(id); it != g_Projectiles.end()) return &it->second.common;
        if (auto it = g_Functions.find(id);   it != g_Functions.end())   return &it->second.common;
        if (auto it = g_Areas.find(id);       it != g_Areas.end())       return &it->second.common;
        if (auto it = g_Frames.find(id);      it != g_Frames.end())      return &it->second.common;
        return nullptr;
    }

    // ---- 登録ヘルパー（種類ごとにオーバーロード）----
    void Register(const ProjectileItemDef& def)
    {
        g_Projectiles[def.common.id] = def;
        g_AllIDs.push_back(def.common.id);
    }
    void Register(const FunctionItemDef& def)
    {
        g_Functions[def.common.id] = def;
        g_AllIDs.push_back(def.common.id);
    }
    void Register(const AreaItemDef& def)
    {
        g_Areas[def.common.id] = def;
        g_AllIDs.push_back(def.common.id);
    }
    void Register(const FrameItemDef& def)
    {
        g_Frames[def.common.id] = def;
        g_AllIDs.push_back(def.common.id);
    }
    // 能力値は g_AllIDs に入れない。背包・呪文書・デバッグの一覧は GetAllIDs を回すので、
    // 入れると「背包に置ける物」として扱われてしまう
    void Register(const StatItemDef& def)
    {
        g_Stats[def.common.id] = def;
        g_LevelUpOnlyIDs.push_back(def.common.id);
    }
}

// ============================================================
// 初期化
// 起動時に1回だけ呼ぶ想定。二回目以降は何もしない（多重登録防止）
// ============================================================
void ItemDatabase::Initialize()
{
    if (g_Initialized) return;

    g_Projectiles.clear();
    g_Functions.clear();
    g_Areas.clear();
    g_Frames.clear();
    g_Stats.clear();
    g_AllIDs.clear();
    g_LevelUpOnlyIDs.clear();

    // ---- 飛行物型 ----
    Register(MakeFireball());
    Register(MakeArcBolt());
    Register(MakeHomingBolt());
    Register(MakeMeteor());
    Register(MakeGoldenArrow());
    Register(MakeStoneShot());
    Register(MakePoison());
    Register(MakeBeam());

    // ---- 機能型 ----
    Register(MakeSplitRune());
    Register(MakeDoubleCastRune());
    Register(MakeMagnifier());
    Register(MakeHasteRune());

    // ---- AOE 型（未実装）----

    // ---- 設置枠 ----
    Register(MakeFrame3x3());

    // ---- 能力値（レベルアップ専用）----
    Register(MakeMaxHealthUp());
    Register(MakeMaxManaUp());
    Register(MakeMoveSpeedUp());
    Register(MakeJumpPowerUp());
    Register(MakeManaRegenUp());
    Register(MakeJumpCountUp());

    // ---- 形（占位格・影響格）----
    // コードに書いた形を覚えてから、保存済みの道具データ（ItemData/<名前>.json）で上書きする。
    // 能力値は背包に置かないので形を持たない（g_AllIDs に入っていない）
    g_CodeShapes.clear();
    int shapeFiles = 0;
    for (ItemID id : g_AllIDs)
    {
        ItemCommon* c = MutableCommon(id);
        if (!c) continue;
        g_CodeShapes[id] = { c->occupyCells, c->influenceCells };
        if (ItemDataFile::LoadShape(c->name, c->occupyCells, c->influenceCells))
            ++shapeFiles;
    }
    std::cout << "[ItemDatabase] shape files: " << shapeFiles << " (" << ItemDataFile::kDir << ")" << std::endl;

    g_Initialized = true;
    std::cout << "[OK] ItemDatabase initialized ("
        << g_Projectiles.size() << " projectile, "
        << g_Functions.size() << " function, "
        << g_Areas.size() << " area, "
        << g_Frames.size() << " frame, "
        << g_Stats.size() << " stat)" << std::endl;
}

// ============================================================
// 検索
// ============================================================
ItemCategory ItemDatabase::GetCategory(ItemID id)
{
    if (g_Projectiles.count(id)) return ItemCategory::Projectile;
    if (g_Functions.count(id))   return ItemCategory::Function;
    if (g_Areas.count(id))       return ItemCategory::Area;
    if (g_Frames.count(id))      return ItemCategory::Frame;
    if (g_Stats.count(id))       return ItemCategory::Stat;

    // どこにも無ければ Unknown。
    // 未登録の ID を渡した呼び出し側の不具合をここで拾えるようにする
    return ItemCategory::Unknown;
}

const ProjectileItemDef* ItemDatabase::GetProjectile(ItemID id)
{
    auto it = g_Projectiles.find(id);
    return (it != g_Projectiles.end()) ? &it->second : nullptr;
}

const FunctionItemDef* ItemDatabase::GetFunction(ItemID id)
{
    auto it = g_Functions.find(id);
    return (it != g_Functions.end()) ? &it->second : nullptr;
}

const AreaItemDef* ItemDatabase::GetArea(ItemID id)
{
    auto it = g_Areas.find(id);
    return (it != g_Areas.end()) ? &it->second : nullptr;
}

const FrameItemDef* ItemDatabase::GetFrame(ItemID id)
{
    auto it = g_Frames.find(id);
    return (it != g_Frames.end()) ? &it->second : nullptr;
}

const StatItemDef* ItemDatabase::GetStat(ItemID id)
{
    auto it = g_Stats.find(id);
    return (it != g_Stats.end()) ? &it->second : nullptr;
}

const ItemCommon* ItemDatabase::GetCommon(ItemID id)
{
    if (auto* p = GetProjectile(id)) return &p->common;
    if (auto* f = GetFunction(id))   return &f->common;
    if (auto* a = GetArea(id))       return &a->common;
    if (auto* fr = GetFrame(id))     return &fr->common;
    if (auto* s = GetStat(id))       return &s->common;
    return nullptr;
}

bool ItemDatabase::IsAttackType(ItemID id)
{
    ItemCategory c = GetCategory(id);
    return c == ItemCategory::Projectile || c == ItemCategory::Area;
}

bool ItemDatabase::IsFrame(ItemID id)
{
    return GetCategory(id) == ItemCategory::Frame;
}

const std::vector<ItemID>& ItemDatabase::GetAllIDs()
{
    return g_AllIDs;
}

const std::vector<ItemID>& ItemDatabase::GetLevelUpOnlyIDs()
{
    return g_LevelUpOnlyIDs;
}

// ============================================================
// 形の編集
// ============================================================
void ItemDatabase::SetShape(ItemID id, const std::vector<CellOffset>& occupy,
    const std::vector<CellOffset>& influence)
{
    ItemCommon* c = MutableCommon(id);
    if (!c) return;
    c->occupyCells = occupy;
    c->influenceCells = influence;
}

bool ItemDatabase::GetCodeShape(ItemID id, std::vector<CellOffset>& occupy,
    std::vector<CellOffset>& influence)
{
    auto it = g_CodeShapes.find(id);
    if (it == g_CodeShapes.end()) return false;
    occupy = it->second.first;
    influence = it->second.second;
    return true;
}

// ============================================================
// 飛行物型への修飾を適用する
// パラメータが増えても switch に case を1個足すだけで済む
// ============================================================
void ItemDatabase::ApplyModifier(SpellStats& stats, const ParamModifier& mod)
{
    float* target = nullptr;
    int* targetInt = nullptr;

    switch (mod.param)
    {
    case SpellParam::Damage:          target = &stats.damage;          break;
    case SpellParam::Speed:           target = &stats.speed;           break;
    case SpellParam::Radius:          target = &stats.radius;          break;
    case SpellParam::Lifetime:        target = &stats.lifetime;        break;
    case SpellParam::SpreadAngle:     target = &stats.spreadAngle;     break;
    case SpellParam::CastDelay:       target = &stats.castDelay;       break;
    case SpellParam::CastInterval:    target = &stats.castInterval;    break;
    case SpellParam::ManaCost:        target = &stats.manaCost;        break;
    case SpellParam::ProjectileCount: targetInt = &stats.projectileCount; break;
    case SpellParam::CastCount:       targetInt = &stats.castCount;       break;

    default:
        return;
    }

    if (target)
    {
        switch (mod.op)
        {
        case ModifyOp::Add:      *target += mod.value; break;
        case ModifyOp::Multiply: *target *= mod.value; break;
        case ModifyOp::Set:      *target = mod.value; break;
        }
        if (*target < 0.0f) *target = 0.0f;   // 負値は許さない
    }
    else if (targetInt)
    {
        switch (mod.op)
        {
        case ModifyOp::Add:      *targetInt += (int)mod.value; break;

            // 四捨五入してから丸める。
            // 切り捨てだと 1 × 0.6 = 0 になってしまい、
            // 「弾数を減らす」意図の倍率が「弾を消す」に化けるのを防ぐ
        case ModifyOp::Multiply:
            *targetInt = (int)(*targetInt * mod.value + 0.5f);
            break;

        case ModifyOp::Set:      *targetInt = (int)mod.value; break;
        }
        if (*targetInt < 1) *targetInt = 1;   // 最低1は確保する
    }
}
// ============================================================
// AOE 型への修飾を適用する
// SpellStats 側とは完全に独立した処理。
// パラメータが増えても switch に case を1個足すだけで済む
// ============================================================
void ItemDatabase::ApplyModifier(AreaStats& stats, const AreaModifier& mod)
{
    float* target = nullptr;

    switch (mod.param)
    {
    case AreaParam::Radius:        target = &stats.radius;        break;
    case AreaParam::Duration:      target = &stats.duration;      break;
    case AreaParam::TickInterval:  target = &stats.tickInterval;  break;
    case AreaParam::DamagePerTick: target = &stats.damagePerTick; break;
    case AreaParam::CastInterval:  target = &stats.castInterval;  break;
    case AreaParam::ManaCost:      target = &stats.manaCost;      break;

    default:
        return;
    }

    switch (mod.op)
    {
    case ModifyOp::Add:      *target += mod.value; break;
    case ModifyOp::Multiply: *target *= mod.value; break;
    case ModifyOp::Set:      *target = mod.value; break;
    }

    if (*target < 0.0f) *target = 0.0f;   // 負値は許さない
}
