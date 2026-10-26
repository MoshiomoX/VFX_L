// ============================================================
// ItemDatabase.cpp
// ============================================================
#include "Item/ItemDatabase.h"
#include "Swarm/SwarmTypes.h"

// ---- 各アイテムの定義（増やす時はここに include を1行）----
#include "Item/Items/Fireball.h"
#include "Item/Items/ArcBolt.h"
#include "Item/Items/HomingBolt.h"
#include "Item/Items/GoldenArrow.h"
#include "Item/Items/Meteor.h"
#include "Item/Items/StoneShot.h"
#include "Item/Items/Poison.h"
#include "Item/Items/Swamp.h"
#include "Item/Items/Spore.h"
#include "Item/Items/VoidVortex.h"
#include "Item/Items/IceLance.h"
#include "Item/Items/IceSpike.h"
#include "Item/Items/Beam.h"
#include "Item/Items/Gust.h"
#include "Item/Items/Twister.h"
#include "Item/Items/SplitRune.h"
#include "Item/Items/DoubleCastRune.h"
#include "Item/Items/Magnifier.h"
#include "Item/Items/HasteRune.h"
#include "Item/Items/ChainRune.h"
#include "Item/Items/LifestealRune.h"
#include "Item/Items/CrystalBall.h"
#include "Item/Items/Frame3x3.h"
#include "Item/Items/MaxHealthUp.h"
#include "Item/Items/MaxManaUp.h"
#include "Item/Items/MoveSpeedUp.h"
#include "Item/Items/ManaRegenUp.h"
#include "Item/Items/JumpCountUp.h"
#include "Item/Items/SpellPowerUp.h"
#include "Item/Items/ShieldUp.h"
#include "Item/ItemDataFile.h"
#include "Swarm/AreaProfile.h"   // AreaCanChain（溜め 0 の光線か）

namespace
{
    // 種類ごとの登録所（実体はここだけが持つ）
    std::unordered_map<ItemID, ProjectileItemDef> g_Projectiles;
    std::unordered_map<ItemID, FunctionItemDef>   g_Functions;
    std::unordered_map<ItemID, AreaItemDef>       g_Areas;
    std::unordered_map<ItemID, FrameItemDef>      g_Frames;
    std::unordered_map<ItemID, SummonItemDef>     g_Summons;   // 召喚物（水晶玉。2026-10-06）
    std::unordered_map<ItemID, StatItemDef>       g_Stats;

    std::vector<ItemID> g_AllIDs;          // 全ID（UI の一覧表示用）。能力値は入れない
    std::vector<ItemID> g_LevelUpOnlyIDs;  // レベルアップの候補にだけ出る物（能力値）
    bool g_Initialized = false;

    // Items/*.h に書いてある形（json で上書きする前）。エディタの「コードの既定に戻す」用
    std::unordered_map<ItemID,
        std::pair<std::vector<CellOffset>, std::vector<CellOffset>>> g_CodeShapes;

    // ItemID の番号で引く平らな表（2026-10-25）。GetCommon / GetCategory / IsFrame は描画・判定の
    // 毎フレームの処理から何千回も呼ばれるので、種類ごとの表を最大 6 回引かずにここを 1 回見る。
    // 中身は上の登録所の要素を指すだけ（unordered_map の要素は再ハッシュでも動かない）。
    // 登録所が変わるのは Initialize の中だけなので、そこで作り直す
    struct FlatEntry
    {
        ItemCategory category = ItemCategory::Unknown;
        const ItemCommon*        common = nullptr;
        const ProjectileItemDef* projectile = nullptr;
        const FunctionItemDef*   function = nullptr;
        const AreaItemDef*       area = nullptr;
        const FrameItemDef*      frame = nullptr;
        const SummonItemDef*     summon = nullptr;
        const StatItemDef*       stat = nullptr;
    };
    std::vector<FlatEntry> g_Flat;

    const FlatEntry* FlatOf(ItemID id)
    {
        const int i = (int)id;
        return (i >= 0 && i < (int)g_Flat.size()) ? &g_Flat[i] : nullptr;
    }

    // 種類と共通部分の優先順は前の GetCategory / GetCommon と同じ（飛行物 → 機能 → 範囲 → 枠 → 召喚物 → 能力値）
    void BuildFlat()
    {
        g_Flat.clear();
        int maxId = -1;
        auto grow = [&](ItemID id) { maxId = (std::max)(maxId, (int)id); };
        for (const auto& kv : g_Projectiles) grow(kv.first);
        for (const auto& kv : g_Functions)   grow(kv.first);
        for (const auto& kv : g_Areas)       grow(kv.first);
        for (const auto& kv : g_Frames)      grow(kv.first);
        for (const auto& kv : g_Summons)     grow(kv.first);
        for (const auto& kv : g_Stats)       grow(kv.first);
        if (maxId < 0) return;
        g_Flat.resize((size_t)maxId + 1);

        auto claim = [](FlatEntry& e, ItemCategory c, const ItemCommon* common)
        {
            if (e.common) return;   // 先に見つかった種類を優先
            e.category = c;
            e.common = common;
        };
        for (const auto& kv : g_Projectiles) { auto& e = g_Flat[(int)kv.first]; e.projectile = &kv.second; claim(e, ItemCategory::Projectile, &kv.second.common); }
        for (const auto& kv : g_Functions)   { auto& e = g_Flat[(int)kv.first]; e.function = &kv.second;   claim(e, ItemCategory::Function, &kv.second.common); }
        for (const auto& kv : g_Areas)       { auto& e = g_Flat[(int)kv.first]; e.area = &kv.second;       claim(e, ItemCategory::Area, &kv.second.common); }
        for (const auto& kv : g_Frames)      { auto& e = g_Flat[(int)kv.first]; e.frame = &kv.second;      claim(e, ItemCategory::Frame, &kv.second.common); }
        for (const auto& kv : g_Summons)     { auto& e = g_Flat[(int)kv.first]; e.summon = &kv.second;     claim(e, ItemCategory::Summon, &kv.second.common); }
        for (const auto& kv : g_Stats)       { auto& e = g_Flat[(int)kv.first]; e.stat = &kv.second;       claim(e, ItemCategory::Stat, &kv.second.common); }
    }

    // 形が変わった回数（SetShape / Initialize で進む）。UI が形から作った物を覚えておく時の目印
    uint32_t g_ShapeRevision = 1;

    // 形の差し替え用。外へは const でしか出さない
    ItemCommon* MutableCommon(ItemID id)
    {
        if (auto it = g_Projectiles.find(id); it != g_Projectiles.end()) return &it->second.common;
        if (auto it = g_Functions.find(id);   it != g_Functions.end())   return &it->second.common;
        if (auto it = g_Areas.find(id);       it != g_Areas.end())       return &it->second.common;
        if (auto it = g_Frames.find(id);      it != g_Frames.end())      return &it->second.common;
        if (auto it = g_Summons.find(id);     it != g_Summons.end())     return &it->second.common;
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
    void Register(const SummonItemDef& def)
    {
        g_Summons[def.common.id] = def;
        g_AllIDs.push_back(def.common.id);
    }
    // 能力値は g_AllIDs に入れない。バックパック・呪文書・デバッグの一覧は GetAllIDs を回すので、
    // 入れると「バックパックに置ける物」として扱われてしまう
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
    g_Summons.clear();
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
    Register(MakeSwamp());
    Register(MakeSpore());
    Register(MakeVoidVortex());
    Register(MakeIceLance());
    Register(MakeIceSpike());
    Register(MakeBeam());
    Register(MakeGust());
    Register(MakeTwister());

    // ---- 機能型 ----
    Register(MakeSplitRune());
    Register(MakeDoubleCastRune());
    Register(MakeMagnifier());
    Register(MakeHasteRune());
    Register(MakeChainRune());
    Register(MakeLifestealRune());

    // ---- 召喚物（2026-10-06）----
    Register(MakeCrystalBall());

    // ---- AOE 型（未実装）----

    // ---- 設置枠 ----
    Register(MakeFrame3x3());

    // ---- 能力値（レベルアップ専用）----
    Register(MakeMaxHealthUp());
    Register(MakeMaxManaUp());
    Register(MakeMoveSpeedUp());
    // 跳躍力 +8%（JumpPowerUp）は 2026-10-26 に削除（ユーザー「釣り合いを壊している」）。ItemID / StatKind::JumpPower は残る（引き継ぎの stats.jumpPower は変えない）
    Register(MakeManaRegenUp());
    Register(MakeJumpCountUp());
    Register(MakeSpellPowerUp());
    Register(MakeShieldUp());

    // ---- 形（占有マス・影響マス）----
    // コードに書いた形を覚えてから、保存済みのアイテムデータ（ItemData/<名前>.json）で上書きする。
    // 能力値はバックパックに置かないので形を持たない（g_AllIDs に入っていない）
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

    BuildFlat();
    ++g_ShapeRevision;
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
// どれも ItemID の番号の平らな表（BuildFlat）を 1 回引くだけ。Initialize の前は表が空なので全部 null / Unknown
// ============================================================
ItemCategory ItemDatabase::GetCategory(ItemID id)
{
    // どこにも無ければ Unknown。
    // 未登録の ID を渡した呼び出し側の不具合をここで拾えるようにする
    const FlatEntry* e = FlatOf(id);
    return e ? e->category : ItemCategory::Unknown;
}

const ProjectileItemDef* ItemDatabase::GetProjectile(ItemID id)
{
    const FlatEntry* e = FlatOf(id);
    return e ? e->projectile : nullptr;
}

const FunctionItemDef* ItemDatabase::GetFunction(ItemID id)
{
    const FlatEntry* e = FlatOf(id);
    return e ? e->function : nullptr;
}

const AreaItemDef* ItemDatabase::GetArea(ItemID id)
{
    const FlatEntry* e = FlatOf(id);
    return e ? e->area : nullptr;
}

const FrameItemDef* ItemDatabase::GetFrame(ItemID id)
{
    const FlatEntry* e = FlatOf(id);
    return e ? e->frame : nullptr;
}

const SummonItemDef* ItemDatabase::GetSummon(ItemID id)
{
    const FlatEntry* e = FlatOf(id);
    return e ? e->summon : nullptr;
}

const StatItemDef* ItemDatabase::GetStat(ItemID id)
{
    const FlatEntry* e = FlatOf(id);
    return e ? e->stat : nullptr;
}

const ItemCommon* ItemDatabase::GetCommon(ItemID id)
{
    const FlatEntry* e = FlatOf(id);
    return e ? e->common : nullptr;
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
    ++g_ShapeRevision;   // 平らな表は要素を指しているだけなので作り直さなくて良い（中身がここで変わる）
}

uint32_t ItemDatabase::ShapeRevision()
{
    return g_ShapeRevision;
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
    case SpellParam::AreaDamage:      target = &stats.areaDamageMul;   break;
    case SpellParam::ProjectileCount: targetInt = &stats.projectileCount; break;
    case SpellParam::CastCount:       targetInt = &stats.castCount;       break;
    case SpellParam::Lifesteal:       target = &stats.lifesteal;       break;
    case SpellParam::ChainCount:      targetInt = &stats.chainCount;      break;

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
        if (mod.param == SpellParam::Lifesteal) *target = (std::min)(*target, Swarm::kLifestealRatio);   // 重ねても増えない（GPU は印だけ）
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
        // 最低 1 は確保する（弾数・連射）。連鎖は 0 = 跳ばない、なので 0 まで
        const int lowest = (mod.param == SpellParam::ChainCount) ? 0 : 1;
        if (*targetInt < lowest) *targetInt = lowest;
    }
}
// ============================================================
// AOE 型への修飾を適用する
// SpellStats 側とは完全に独立した処理。
// パラメータが増えても switch に case を1個足すだけで済む
// ============================================================
bool ItemDatabase::AreaCanChain(const AreaStats& stats)
{
    if (stats.profile <= 0) return false;
    const AreaProfile& ap = AreaProfileDB::At(stats.profile);
    return ap.IsBeam() && ap.chargeTime <= 0.0f;   // 一瞬の稲妻だけ（溜めのある魔導光線は跳ばない）
}

void ItemDatabase::ApplyModifier(AreaStats& stats, const AreaModifier& mod)
{
    float* target = nullptr;

    // 連鎖（2026-10-20）: 跳べない範囲魔法には回数も、連鎖のルーンの代価（chainOnly）も掛けない
    if (mod.param == AreaParam::ChainCount || mod.chainOnly)
        if (!AreaCanChain(stats)) return;
    if (mod.param == AreaParam::ChainCount)
    {
        int& n = stats.chainCount;
        switch (mod.op)
        {
        case ModifyOp::Add:      n += (int)mod.value; break;
        case ModifyOp::Multiply: n = (int)(n * mod.value + 0.5f); break;
        case ModifyOp::Set:      n = (int)mod.value; break;
        }
        if (n < 0) n = 0;   // 0 = 跳ばない
        return;
    }

    switch (mod.param)
    {
    case AreaParam::Radius:        target = &stats.radius;        break;
    case AreaParam::Duration:      target = &stats.duration;      break;
    case AreaParam::TickInterval:  target = &stats.tickInterval;  break;
    case AreaParam::DamagePerTick: target = &stats.damagePerTick; break;
    case AreaParam::CastInterval:  target = &stats.castInterval;  break;
    case AreaParam::ManaCost:      target = &stats.manaCost;      break;
    case AreaParam::Length:        target = &stats.length;        break;   // 光線以外は 0 × 倍率 = 0 のまま
    case AreaParam::Lifesteal:     target = &stats.lifesteal;     break;

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
    if (mod.param == AreaParam::Lifesteal) *target = (std::min)(*target, Swarm::kLifestealRatio);   // 重ねても増えない
}
