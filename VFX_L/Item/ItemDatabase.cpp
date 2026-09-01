// ============================================================
// ItemDatabase.cpp
// ============================================================
#include "Item/ItemDatabase.h"

// ---- ??????????(??????? include ?1?)----
#include "Item/Items/Fireball.h"
#include "Item/Items/SplitRune.h"
#include "Item/Items/DoubleCastRune.h"
#include "Item/Items/Frame3x3.h"
#include <unordered_map>
#include <iostream>

namespace
{
    // ??????????????
    std::unordered_map<ItemID, ProjectileItemDef> g_Projectiles;
    std::unordered_map<ItemID, FunctionItemDef>   g_Functions;
    std::unordered_map<ItemID, AreaItemDef>       g_Areas;
    std::unordered_map<ItemID, FrameItemDef>      g_Frames;

    std::vector<ItemID> g_AllIDs;   // ???(UI ?????)
    bool g_Initialized = false;

    // ---- ?????(??????????)----
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
}

// ============================================================
// ??
// ????????????1????????????????????
// ============================================================
void ItemDatabase::Initialize()
{
    if (g_Initialized) return;

    g_Projectiles.clear();
    g_Functions.clear();
    g_Areas.clear();
    g_Frames.clear();
    g_AllIDs.clear();

    // ---- ???? ----
    Register(MakeFireball());

    // ---- ??? ----
    Register(MakeSplitRune());
    Register(MakeDoubleCastRune());

    // ---- AOE ?(????)----

    // ---- ??? ----
    Register(MakeFrame3x3());

    g_Initialized = true;
    std::cout << "[OK] ItemDatabase initialized ("
        << g_Projectiles.size() << " projectile, "
        << g_Functions.size() << " function, "
        << g_Areas.size() << " area, "
        << g_Frames.size() << " frame)" << std::endl;
}

// ============================================================
// ??
// ============================================================
ItemCategory ItemDatabase::GetCategory(ItemID id)
{
    if (g_Projectiles.count(id)) return ItemCategory::Projectile;
    if (g_Functions.count(id))   return ItemCategory::Function;
    if (g_Areas.count(id))       return ItemCategory::Area;
    if (g_Frames.count(id))      return ItemCategory::Frame;

    // ??????? Projectile ????????
    // ?????????????????????????????????
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

const ItemCommon* ItemDatabase::GetCommon(ItemID id)
{
    if (auto* p = GetProjectile(id)) return &p->common;
    if (auto* f = GetFunction(id))   return &f->common;
    if (auto* a = GetArea(id))       return &a->common;
    if (auto* fr = GetFrame(id))     return &fr->common;
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

// ============================================================
// ?????
// ???????????????? case ?1??????
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
        if (*target < 0.0f) *target = 0.0f;   // ?????
    }
    else if (targetInt)
    {
        switch (mod.op)
        {
        case ModifyOp::Add:      *targetInt += (int)mod.value; break;

            // ????????????????
            // ?????? 1 × 0.6 = 0 ????
            // ????????????????????????????
        case ModifyOp::Multiply:
            *targetInt = (int)(*targetInt * mod.value + 0.5f);
            break;

        case ModifyOp::Set:      *targetInt = (int)mod.value; break;
        }
        if (*targetInt < 1) *targetInt = 1;   // ??1??????????
    }
}
// ============================================================
// AOE ?????
// SpellStats ????????
// ???????????????? case ?1??????
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

    if (*target < 0.0f) *target = 0.0f;   // ?????
}