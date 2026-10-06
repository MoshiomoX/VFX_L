// ============================================================
// SpellLab.cpp
// 魔法の組み合わせを自由に試す実験場（キー操作 + 数値の窓）。説明は SpellLab.h
// ============================================================
#include "Debug/SpellLab.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Component/WandComponent.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Player/LevelComponent.h"
#include "Component/TransformComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemTypes.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/SwarmTypes.h"
#include "Enemy/MobSpawner.h"
#include "World/GridWorld.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr int kGrid = BackpackComponent::GRID;
}

void SpellLab::Update(Registry& reg, Entity player, SwarmSystem& swarm, const GridWorld& grid)
{
    if (!m_Enabled || !reg.IsValid(player)) return;
    // 経験値は毎フレーム 0 に戻す（レベルアップの四択が実験の邪魔をしない）。MP は満タンを保つ
    if (reg.Has<LevelComponent>(player)) reg.Get<LevelComponent>(player).experience = 0.0f;
    if (reg.Has<ManaComponent>(player))
    {
        auto& mp = reg.Get<ManaComponent>(player);
        mp.max = 1.0e6f;
        mp.current = mp.max;
    }
    // 的のキー（ImGui の入力欄に打っている間は取らない）
    if (ImGui::GetIO().WantTextInput) return;
    const InputManager& in = InputManager::Get();
    if (in.GetKeyTrigger('T')) SpawnTargets(reg, player, swarm, grid, 0);
    if (in.GetKeyTrigger('Y')) SpawnTargets(reg, player, swarm, grid, 1);
    if (in.GetKeyTrigger('U')) SpawnTargets(reg, player, swarm, grid, 2);
    if (in.GetKeyTrigger('O')) SpawnTargets(reg, player, swarm, grid, 3);
    if (in.GetKeyTrigger('K')) { swarm.KillAll(); snprintf(m_Message, sizeof(m_Message), "killed all"); }
}

// 全部の魔法・ルーン（枠・能力カード以外）を各 kCopies 個ずつ魔法書へ。既に持っている分は足りない分だけ足す
void SpellLab::FillChest(Registry& reg, Entity player)
{
    auto& book = reg.Get<SpellbookComponent>(player);
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCategory c = ItemDatabase::GetCategory(id);
        if (c != ItemCategory::Projectile && c != ItemCategory::Area && c != ItemCategory::Function && c != ItemCategory::Summon) continue;
        const int have = book.GetCount(id);
        if (have < kCopies) book.Learn(id, kCopies - have);
    }
}

void SpellLab::FillFrames(Registry& reg, Entity player)
{
    auto& bp = reg.Get<BackpackComponent>(player);
    auto& book = reg.Get<SpellbookComponent>(player);
    // 3x3 の枠を 9 枚敷き詰める（既に枠がある所は飛ばす）。所持数も一緒に増やす
    for (int r = 0; r < kGrid; r += 3)
        for (int c = 0; c < kGrid; c += 3)
            if (BackpackLogic::CanPlaceFrame(bp, ItemID::Frame3x3, r, c, 0))
            {
                if (book.GetCount(ItemID::Frame3x3) <= BackpackLogic::CountPlacedFrames(bp, ItemID::Frame3x3))
                    book.Learn(ItemID::Frame3x3);
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
            }
    bp.dirty = true;
}

void SpellLab::SetEnabled(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs, bool on)
{
    if (on == m_Enabled) return;
    m_Enabled = on;
    if (on)
    {
        m_WasSpawning = mobs.Director().enabled;
        mobs.Director().enabled = false;
        swarm.KillAll();
        if (reg.Has<HealthComponent>(player))
        {
            auto& hp = reg.Get<HealthComponent>(player);
            m_WasInvincible = hp.invincible;
            hp.invincible = true;
        }
        if (reg.Has<ManaComponent>(player)) m_WasManaMax = reg.Get<ManaComponent>(player).max;
    }
    else
    {
        mobs.Director().enabled = m_WasSpawning;
        if (reg.Has<HealthComponent>(player)) reg.Get<HealthComponent>(player).invincible = m_WasInvincible;
        if (reg.Has<ManaComponent>(player))
        {
            auto& mp = reg.Get<ManaComponent>(player);
            mp.max = m_WasManaMax;
            mp.current = std::fmin(mp.current, mp.max);
        }
    }
}

void SpellLab::EnterLab(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs)
{
    if (!reg.Has<BackpackComponent>(player) || !reg.Has<SpellbookComponent>(player)) return;
    SetEnabled(reg, player, swarm, mobs, true);
    FillFrames(reg, player);
    FillChest(reg, player);
}

void SpellLab::SpawnTargets(Registry& reg, Entity player, SwarmSystem& swarm, const GridWorld& grid, int pattern)
{
    if (!reg.Has<TransformComponent>(player)) return;
    const Vector3 pp = reg.Get<TransformComponent>(player).position;
    const float groundY = swarm.GetAIParams().groundY;
    auto put = [&](float dx, float dz, uint32_t kind)
    {
        const Vector3 p(pp.x + dx, grid.SampleHeight(pp.x + dx, pp.z + dz) + groundY, pp.z + dz);
        swarm.SpawnEnemy(p, m_TargetHp, 0.0f, kind);
    };
    switch (pattern)
    {
    case 0:   // 正面（+Z）8〜9m に 3 体。自動テストの magnifier と同じ並び
        put(0.0f, 8.0f, Swarm::kEnemyKindMob);
        put(-1.5f, 9.0f, Swarm::kEnemyKindMob);
        put(1.5f, 9.0f, Swarm::kEnemyKindMob);
        snprintf(m_Message, sizeof(m_Message), "3 targets ahead");
        break;
    case 1:   // 周り 7m の輪に 12 体（範囲魔法・連鎖の見比べ用）
        for (int i = 0; i < 12; ++i)
        {
            const float a = (float)i / 12.0f * 6.2831853f;
            put(std::sin(a) * 7.0f, std::cos(a) * 7.0f, Swarm::kEnemyKindMob);
        }
        snprintf(m_Message, sizeof(m_Message), "ring of 12");
        break;
    case 2: put(0.0f, 9.0f, Swarm::kEnemyKindElite); snprintf(m_Message, sizeof(m_Message), "elite"); break;
    case 3: put(0.0f, 11.0f, Swarm::kEnemyKindBoss); snprintf(m_Message, sizeof(m_Message), "boss"); break;
    default: break;
    }
}

void SpellLab::DrawImGui(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs)
{
    if (!ImGui::CollapsingHeader("Spell Lab")) return;
    bool on = m_Enabled;
    if (ImGui::Checkbox("Lab Mode (no spawns, invincible, infinite MP, no level-ups; keys below work while on)", &on))
        SetEnabled(reg, player, swarm, mobs, on);
    if (m_Enabled && reg.Has<BackpackComponent>(player) && reg.Has<SpellbookComponent>(player))
    {
        if (ImGui::Button("Fill 9x9 with frames")) FillFrames(reg, player);
        ImGui::SameLine();
        if (ImGui::Button("All items into chest")) FillChest(reg, player);
    }
    DrawBody(reg, player);
}

void SpellLab::DrawWindow(Registry& reg, Entity player)
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Spell Lab (F7)"))
    {
        ImGui::TextDisabled("Flat field, no spawns, invincible, infinite MP, no level-ups.");
        ImGui::TextDisabled("Every spell / rune x%d is in the chest: press Tab and build the backpack yourself.", kCopies);
        DrawBody(reg, player);
    }
    ImGui::End();
}

void SpellLab::DrawBody(Registry& reg, Entity player)
{
    if (!reg.IsValid(player) || !reg.Has<BackpackComponent>(player) || !reg.Has<WandComponent>(player))
    {
        ImGui::TextDisabled("(no player)");
        return;
    }
    ImGui::SeparatorText("Targets (key)");
    ImGui::TextDisabled("[T] 3 ahead   [Y] ring of 12   [U] elite   [O] boss   [K] kill all");
    ImGui::Text("Target HP"); ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputFloat("##hp", &m_TargetHp, 0.0f, 0.0f, "%.0f");
    if (m_Message[0]) { ImGui::SameLine(); ImGui::TextDisabled("%s", m_Message); }

    ImGui::SeparatorText("Aggregated wand");
    DrawStats(reg, player);
}

void SpellLab::DrawStats(Registry& reg, Entity player)
{
    const auto& bp = reg.Get<BackpackComponent>(player);
    const auto& wand = reg.Get<WandComponent>(player);

    ImGui::Text("%d items placed: %d projectile spells, %d area spells",
        (int)bp.items.size(), (int)wand.spells.size(), (int)wand.areas.size());
    if (ImGui::BeginTable("##lab_spells", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("spell");
        ImGui::TableSetupColumn("interval");
        ImGui::TableSetupColumn("MP");
        ImGui::TableSetupColumn("damage");
        ImGui::TableSetupColumn("radius");
        ImGui::TableSetupColumn("count");
        ImGui::TableSetupColumn("trigger");
        ImGui::TableHeadersRow();
        for (const SpellStats& s : wand.spells)
        {
            const ItemCommon* ic = ItemDatabase::GetCommon(s.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ic ? ic->name : "?");
            ImGui::TableNextColumn(); ImGui::Text("%.3f", s.castInterval);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", s.manaCost);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", s.damage);
            ImGui::TableNextColumn(); ImGui::Text("%.3f", s.radius);
            ImGui::TableNextColumn(); ImGui::Text("%d", s.castCount);
            ImGui::TableNextColumn();
            if (s.storeUnit >= 0) ImGui::Text("stored in orb unit %d", s.storeUnit);
            else if (s.triggered) ImGui::TextUnformatted("advanced (armed)");
            else if (s.triggerMask)
            {
                // 消えた所で連鎖する上級魔法の名前
                char buf[160] = "drives:";
                for (int k = 0; k < 16; ++k)
                    if (((s.triggerMask >> k) & 1u) && k < (int)wand.spells.size())
                    {
                        const ItemCommon* t = ItemDatabase::GetCommon(wand.spells[k].id);
                        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s", t ? t->name : "?");
                    }
                for (int k = 0; k < 16; ++k)
                    if (((s.triggerMask >> (16 + k)) & 1u) && k < (int)wand.areas.size())
                    {
                        const ItemCommon* t = ItemDatabase::GetCommon(wand.areas[k].id);
                        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s", t ? t->name : "?");
                    }
                ImGui::TextUnformatted(buf);
            }
            else ImGui::TextDisabled("-");
        }
        for (const AreaStats& a : wand.areas)
        {
            const ItemCommon* ic = ItemDatabase::GetCommon(a.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s (area)", ic ? ic->name : "?");
            ImGui::TableNextColumn(); ImGui::Text("%.2f", a.castInterval);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", a.manaCost);
            ImGui::TableNextColumn(); ImGui::Text("%.1f /%.2fs x%.1fs", a.damagePerTick, a.tickInterval, a.duration);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", a.radius);
            ImGui::TableNextColumn(); ImGui::TextUnformatted("-");
            ImGui::TableNextColumn();
            if (a.storeUnit >= 0) ImGui::Text("stored in orb unit %d", a.storeUnit);
            else ImGui::TextUnformatted(a.triggered ? "advanced (armed)" : "-");
        }
        ImGui::EndTable();
    }
    // 召喚物（水晶玉）：何を貯蔵しているか・光球の設定
    for (size_t u = 0; u < wand.orbs.size(); ++u)
    {
        const OrbUnitStats& o = wand.orbs[u];
        ImGui::Text("orb unit %d: %d stored, %d orbs x %.1fs every %.1fs, color (%.2f %.2f %.2f)",
            (int)u, o.storedCount, o.maxOrbs, o.orbLife, o.orbInterval, o.color.x, o.color.y, o.color.z);
    }

    // 置いてあるが発動していない上級魔法（前提の基本魔法が足りない）
    bool anyDormant = false;
    for (int i = 0; i < (int)bp.items.size(); ++i)
    {
        const ItemCommon* ic = ItemDatabase::GetCommon(bp.items[i].id);
        if (!ic || ic->triggeredBy.empty() || BackpackLogic::IsTriggerReady(bp, i)) continue;
        if (!anyDormant) { ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Dormant advanced spells:"); anyDormant = true; }
        char buf[200];
        snprintf(buf, sizeof(buf), "  %s needs:", ic->name);
        for (ItemID need : ic->triggeredBy)
        {
            const ItemCommon* n = ItemDatabase::GetCommon(need);
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s", n ? n->name : "?");
        }
        ImGui::TextUnformatted(buf);
    }
}
