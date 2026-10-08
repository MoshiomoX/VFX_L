// ============================================================
// SpellLab.cpp
// 魔法の組み合わせを自由に試す実験場（トレーニングのメニューの依頼の実行 + 数値の窓）。説明は SpellLab.h
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
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
#include "Enemy/StageDirector.h"
#include "UI/TrainingMenuUI.h"
#include "World/GridWorld.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr int kGrid = BackpackComponent::GRID;
}

void SpellLab::Update(Registry& reg, Entity player, SwarmSystem& swarm, const MobSpawner& mobs, const GridWorld& grid)
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
    // 的・群れ・Boss はトレーニングのメニュー（T / パッド RB → Apply）。2026-10-07 まではここで T / Y / U / O / K のキーを読んでいた
    SpawnSwarmStep(reg, player, swarm, mobs, grid);
}

// ============================================================
// トレーニングのメニューの依頼（UI/TrainingMenuUI）。一時停止中にも呼ばれる：
// 雑魚・的の生成依頼は GPU へ積むだけ（次の gameplay の Flush で湧く）、群れは Update で少しずつ
// ============================================================
std::wstring SpellLab::Apply(const TrainingRequest& q, Registry& reg, Entity player, SwarmSystem& swarm,
    MobSpawner& mobs, StageDirector& stage, const GridWorld& grid)
{
    if (!reg.IsValid(player)) return L"";
    switch (q.kind)
    {
    case TrainingRequest::Kind::Targets:
        SpawnTargets(reg, player, swarm, grid, q.pattern, q.targetHp);
        return (q.pattern == 0) ? L"正面に的を 3 体置いた"
            : (q.pattern == 1) ? L"周りに的を 12 体置いた" : L"エリートの的を置いた";

    case TrainingRequest::Kind::Swarm:
    {
        m_SwarmLeft += q.count;
        m_SwarmKind = q.swarmKind;
        m_SwarmMoving = q.moving;
        std::wstring s = (q.swarmKind >= TrainingMenuUI::kKindChoices - 1) ? std::wstring(L"混合の群れ")
            : std::wstring(TrainingMenuUI::kSwarmKinds[std::clamp(q.swarmKind, 0, TrainingMenuUI::kKindChoices - 1)]);
        s += L"を " + std::to_wstring(q.count) + L" 体出した";
        if (!q.moving) s += L"（止まったまま）";
        return s;
    }

    case TrainingRequest::Kind::Boss:
        if (!stage.RequestBoss(q.moving)) return L"ボスはもう出ている";
        return q.moving ? L"ボスを呼んだ" : L"ボスを呼んだ（止まったまま）";

    case TrainingRequest::Kind::KillAll:
        swarm.KillAll();
        m_SwarmLeft = 0;
        return L"敵を全部消した";

    case TrainingRequest::Kind::AddItem:
    {
        if (!reg.Has<SpellbookComponent>(player)) return L"";
        auto& book = reg.Get<SpellbookComponent>(player);
        book.Learn(q.item, 1);
        const ItemCommon* ic = ItemDatabase::GetCommon(q.item);
        return std::wstring(ic ? ic->displayName : L"?") + L" を木箱に入れた（所持 " + std::to_wstring(book.GetCount(q.item)) + L"）";
    }

    case TrainingRequest::Kind::AddAll:
        if (!reg.Has<SpellbookComponent>(player)) return L"";
        FillChest(reg, player);
        return L"全部の魔法・ルーンを各 " + std::to_wstring(kCopies) + L" 個にした";

    case TrainingRequest::Kind::ClearChest:
        if (!reg.Has<SpellbookComponent>(player) || !reg.Has<BackpackComponent>(player)) return L"";
        ClearChest(reg, player);
        return L"木箱を空にした（バックパックに置いた物は残る）";
    }
    return L"";
}

// 箱の中 = 所持数のうちバックパックに置いていない分。魔法・ルーン・召喚物だけ（枠は残す）
void SpellLab::ClearChest(Registry& reg, Entity player)
{
    auto& book = reg.Get<SpellbookComponent>(player);
    const auto& bp = reg.Get<BackpackComponent>(player);
    const std::vector<SpellbookComponent::Entry> entries = book.entries;   // Forget が消すので写しで回す
    for (const auto& e : entries)
    {
        const ItemCategory c = ItemDatabase::GetCategory(e.id);
        if (c != ItemCategory::Projectile && c != ItemCategory::Area && c != ItemCategory::Function && c != ItemCategory::Summon) continue;
        const int extra = e.count - BackpackLogic::CountPlaced(bp, e.id);
        if (extra > 0) book.Forget(e.id, extra);
    }
}

// 群れの残りを kSwarmPerFrame ずつ。プレイヤーの周り 12〜24m の歩けるマス
void SpellLab::SpawnSwarmStep(Registry& reg, Entity player, SwarmSystem& swarm, const MobSpawner& mobs, const GridWorld& grid)
{
    if (m_SwarmLeft <= 0 || !reg.Has<TransformComponent>(player)) return;
    const Vector3 pp = reg.Get<TransformComponent>(player).position;
    const float groundY = swarm.GetAIParams().groundY;
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    static const uint32_t kKinds[7] = { Swarm::kEnemyKindMob, Swarm::kEnemyKindBomber, Swarm::kEnemyKindSplitter, Swarm::kEnemyKindBrute,
        Swarm::kEnemyKindCharger, Swarm::kEnemyKindShield, Swarm::kEnemyKindGhost };

    const int n = (std::min)(m_SwarmLeft, kSwarmPerFrame);
    for (int i = 0; i < n; ++i)
    {
        // 混合 = 雑魚 35 : 自爆兵 15 : スプリッター 15 : 重装兵 10 : 突撃兵 10 : 盾兵 10 : 幽霊 5
        uint32_t kind = kKinds[std::clamp(m_SwarmKind, 0, 6)];
        if (m_SwarmKind >= 7)
        {
            static const float kCum[6] = { 0.35f, 0.50f, 0.65f, 0.75f, 0.85f, 0.95f };
            const float r = u01(m_Rng);
            int k = 0;
            while (k < 6 && r >= kCum[k]) ++k;
            kind = kKinds[k];
        }
        float hp = 15.0f, speed = 3.5f;
        mobs.KindStats(kind, hp, speed);
        if (!m_SwarmMoving) speed = 0.0f;

        for (int attempt = 0; attempt < 6; ++attempt)
        {
            const float a = u01(m_Rng) * 6.2831853f;
            const float d = 12.0f + 12.0f * u01(m_Rng);
            const Vector3 p(pp.x + std::sin(a) * d, 0.0f, pp.z + std::cos(a) * d);
            int gx = 0, gz = 0;
            grid.WorldToCell(p, gx, gz);
            if (kind != Swarm::kEnemyKindGhost && !grid.IsWalkable(gx, gz)) continue;   // 幽霊は壁も素通り
            swarm.SpawnEnemy({ p.x, grid.SampleHeight(p.x, p.z) + groundY, p.z }, hp, speed, kind);
            break;
        }
    }
    m_SwarmLeft -= n;
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
    FillFrames(reg, player);   // 木箱は空のまま（2026-10-07。入れるのはトレーニングのメニューから）
}

void SpellLab::SpawnTargets(Registry& reg, Entity player, SwarmSystem& swarm, const GridWorld& grid, int pattern, float hp)
{
    if (!reg.Has<TransformComponent>(player)) return;
    const Vector3 pp = reg.Get<TransformComponent>(player).position;
    const float groundY = swarm.GetAIParams().groundY;
    auto put = [&](float dx, float dz, uint32_t kind)
    {
        const Vector3 p(pp.x + dx, grid.SampleHeight(pp.x + dx, pp.z + dz) + groundY, pp.z + dz);
        swarm.SpawnEnemy(p, hp, 0.0f, kind);
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

void SpellLab::DrawImGui(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs, const GridWorld& grid)
{
    if (!ImGui::CollapsingHeader("Spell Lab")) return;
    bool on = m_Enabled;
    if (ImGui::Checkbox("Lab Mode (no spawns, invincible, infinite MP, no level-ups)", &on))
        SetEnabled(reg, player, swarm, mobs, on);
    if (m_Enabled && reg.Has<BackpackComponent>(player) && reg.Has<SpellbookComponent>(player))
    {
        if (ImGui::Button("Fill 9x9 with frames")) FillFrames(reg, player);
        ImGui::SameLine();
        if (ImGui::Button("All items into chest")) FillChest(reg, player);
        // 的（実験場シーンではトレーニングのメニューから。こちらは普通の戦闘の実験モード用）
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputFloat("Target HP", &m_TargetHp, 0.0f, 0.0f, "%.0f");
        if (ImGui::Button("3 ahead")) SpawnTargets(reg, player, swarm, grid, 0, m_TargetHp);
        ImGui::SameLine();
        if (ImGui::Button("Ring of 12")) SpawnTargets(reg, player, swarm, grid, 1, m_TargetHp);
        ImGui::SameLine();
        if (ImGui::Button("Elite")) SpawnTargets(reg, player, swarm, grid, 2, m_TargetHp);
        ImGui::SameLine();
        if (ImGui::Button("Boss (static)")) SpawnTargets(reg, player, swarm, grid, 3, m_TargetHp);
        ImGui::SameLine();
        if (ImGui::Button("Kill all")) { swarm.KillAll(); snprintf(m_Message, sizeof(m_Message), "killed all"); }
        if (m_Message[0]) ImGui::TextDisabled("%s", m_Message);
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
        ImGui::TextDisabled("Targets / swarms / boss / items: the in-game Training menu (T or pad RB).");
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
