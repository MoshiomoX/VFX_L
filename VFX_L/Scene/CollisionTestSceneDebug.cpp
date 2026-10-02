// ============================================================
// CollisionTestSceneDebug.cpp
// CollisionTestScene の ImGui 面板・デバッグ描画・TEMP-TEST の自測。
// 本体（初期化・毎フレームの流れ・描画）は CollisionTestScene.cpp。
// 出来上がった機能の面板は各部品が持つ（BattleCamera / RewardCrateSystem /
// FeedbackVFXSystem / SceneLighting / EliteSpawner / MobSpawner / StressTestTools）
// ============================================================
#include "Scene/CollisionTestScene.h"

#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Component/WandComponent.h"
#include "Component/InteractableComponent.h"
#include "Component/ModelComponent.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Model/Model.h"
#include <filesystem>
#include "Graphics/Model/SkinnedModel.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerFactory.h"
#include "Player/LevelComponent.h"
#include "ECS/View.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Item/ItemTypes.h"
#include "Item/ItemInfo.h"
#include "UI/LevelUpSystem.h"
#include "Swarm/AreaProfile.h"
#include "World/TerrainGenerator.h"
#include "VFX_Editor/VFXId.h"
#include "Debug/DebugManager.h"
#include "Debug/FrameProfiler.h"
#include "Core/Application.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>   // TEMP-TEST: autotest.log
#include <random>
#include <unordered_set>

namespace
{
    const char* MoveDirName(MoveDirID d)
    {
        switch (d)
        {
        case MoveDirID::Forward: return "Forward";
        case MoveDirID::Right:   return "Right";
        case MoveDirID::Back:    return "Back";
        case MoveDirID::Left:    return "Left";
        default:                 return "-";
        }
    }

    // TEMP-TEST: 自測で背包を組み直す前に、置いてある魔法を全部外す（開局の火球も。手元へ戻るだけ）
    void ClearBackpackItems(BackpackComponent& bp)
    {
        while (!bp.items.empty())
            BackpackLogic::Remove(bp, (int)bp.items.size() - 1);
        bp.dirty = true;
    }
}

// ============================================================
// ImGui: 全体
// ============================================================
void CollisionTestScene::DrawDebugUI()
{
    // 太陽の目印・場景光源のギズモ（背包・升級・呪文書を開いている間はギズモを出さない）
    m_Lighting.DrawMarkers(PlayerPos(), m_Grid, GetCamera(), !m_GameUI.ShouldPauseGame());

    ImGui::Begin("Game Test");
    ImGui::SameLine();
    ImGui::Checkbox("Swarm Debug", &m_ShowSwarmDebug);
    ImGui::Checkbox("Mesh", &m_ShowMesh);
    ImGui::SameLine();
    ImGui::SameLine();
    ImGui::Checkbox("Particle", &m_ShowParticle);
    ImGui::SameLine();
    ImGui::Checkbox("Collider", &m_ShowWireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Wand Debug", &m_ShowWandDebug);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &m_ShowGridDebug);
    ImGui::SameLine();
    if (ImGui::Button("End Run -> Result")) EndRun();   // リザルト画面の確認用
    ImGui::Separator();

    DrawPlayerPanel();
    DrawWandPanel();
    m_GameUI.DrawDebugUI(m_Registry, m_Player, m_BackpackAggregate);
    m_Stress.DrawImGui(m_Swarm, m_CollisionSystem, m_ParticleSystem);
    DrawSwarmPanel();
    DrawItemDatabasePanel();
    DrawTerrainPanel();
    m_Grass.DrawImGui();
    DrawEnemiesPanel();
    if (m_Crates.DrawImGui(m_Interaction, m_LevelUpSystem)) RespawnCrates();
    if (const Vector3* pp = PlayerPos())
        m_Pickups.DrawImGui(m_Registry, m_Grid, *pp);
    m_Feedback.DrawImGui(m_Registry, m_Player);
    m_Camera.DrawImGui();
    m_Lighting.DrawImGui(PlayerPos());
    m_Shadows.DrawImGui();
    FrameProfiler::Get().DrawImGui();
    DrawBloomPanel();
    ImGui::End();
}

// ============================================================
// 衝突体のワイヤ・杖の可視化・格子（UpdateGameplay の最後）
// ============================================================
void CollisionTestScene::DrawGameplayDebug()
{
    if (m_ShowWireframe)
    {
        std::unordered_set<Entity> hitting;
        for (const auto& p : m_CollisionSystem.GetPairs())
        {
            hitting.insert(p.a);
            hitting.insert(p.b);
        }

        m_Registry.CreateView<TransformComponent, ColliderComponent>()
            .Each([&](Entity e, TransformComponent&, ColliderComponent&)
                {
                    Color col = hitting.count(e) ? Color(1.0f, 0.3f, 0.3f, 1.0f)
                        : Color(0.4f, 1.0f, 0.4f, 1.0f);
                    DrawColliderDebug(e, col);
                });
    }

    if (m_ShowWandDebug)
        DrawWandDebug();
    const Vector3* pp = PlayerPos();
    if (m_ShowGridDebug && pp)
        m_Grid.DrawDebug(*pp);
}

// ============================================================
// ImGui: 敵（雑魚の湧き・初期値・AI は MobSpawner、精英は EliteSpawner）
// ============================================================
void CollisionTestScene::DrawEnemiesPanel()
{
    if (!ImGui::CollapsingHeader("Enemies", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    m_Mobs.DrawImGui(m_Swarm);
    ImGui::Separator();
    m_Stage.DrawImGui(m_Swarm, m_RunTime);
    ImGui::Separator();

    if (m_Elites.DrawImGui(m_Registry, m_MeshVFXSystem)) RespawnElites();
    ImGui::SameLine();
    // GPU 側を全消し。counter は残るので kills (total) は減らない
    if (ImGui::Button("Kill All (GPU)")) m_Swarm.KillAll();
}

// ============================================================
// ImGui: 杖（集約の結果を読むだけ）
// ============================================================
void CollisionTestScene::DrawWandPanel()
{
    if (!ImGui::CollapsingHeader("Wand (result of aggregation)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<WandComponent>(m_Player))
        return;

    auto& w = m_Registry.Get<WandComponent>(m_Player);

    // マナは使い手のもの。ここでは持続判定のために regen を読むだけ
    const float regen = m_Registry.Has<ManaComponent>(m_Player)
        ? m_Registry.Get<ManaComponent>(m_Player).regen : 0.0f;

    ImGui::DragFloat("Range", &w.range, 0.5f, 1.0f, 60.0f);

    // ---- 発射の仕方 ----
    int modeIdx = (int)w.castMode;
    const char* modeNames[] = { "Auto", "Manual", "Debug Burst" };
    if (ImGui::Combo("Cast Mode", &modeIdx, modeNames, 3))
        w.castMode = (CastMode)modeIdx;
    ImGui::Checkbox("Casting Paused (debug)", &w.castingPaused);   // 施法停止（2026-10-01 から Q ではなく調試・自測専用）
    // 魔力解放（Q / パッド Y）: 3 秒消費なし、30 秒に 1 回
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        ImGui::Text("Mana surge (Q): %s  left %.1f  cooldown %.1f",
            mp.SurgeActive() ? "ON" : "off", mp.surgeTime, mp.surgeCooldownLeft);
        ImGui::DragFloat("Surge duration (s)", &mp.surgeDuration, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Surge cooldown (s)", &mp.surgeCooldown, 0.5f, 0.0f, 300.0f);
        // 解放中の強化（2026-10-02）: 詠唱の計時の速さ / 持続する範囲の長さ
        ImGui::DragFloat("Surge cast speed x", &mp.surgeCastSpeed, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Surge duration x", &mp.surgeDurationMul, 0.05f, 0.1f, 5.0f);
        if (ImGui::Button("Reset surge cooldown")) mp.surgeCooldownLeft = 0.0f;
    }
    // 能力アップ「魔法威力」の累積（変えたら背包を集約し直す）
    if (m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        auto& st = m_Registry.Get<PlayerStatsComponent>(m_Player);
        if (ImGui::DragFloat("Spell power x", &st.spellPower, 0.01f, 0.1f, 10.0f)
            && m_Registry.Has<BackpackComponent>(m_Player))
            m_Registry.Get<BackpackComponent>(m_Player).dirty = true;
    }

    switch (w.castMode)
    {
    case CastMode::Auto:
        ImGui::TextDisabled("Fires whenever a target is in range.");
        break;
    case CastMode::Manual:
        ImGui::TextDisabled("LMB / Pad X to fire. Aim is still automatic.");
        ImGui::TextColored(w.castRequested ? ImVec4(1, 0.9f, 0.3f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
            "requested: %s", w.castRequested ? "YES" : "no");
        break;
    case CastMode::DebugBurst:
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
            "Ignores cast interval. Mana still applies,");
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
            "so the real rate is bounded by mana regen.");
        break;
    }

    ImGui::Text("Cast Anim : %.2f s", w.castAnimTimer);
    ImGui::DragFloat("Anim Duration", &w.castAnimDuration, 0.01f, 0.05f, 2.0f);

    ImGui::TextDisabled("spells / areas are read-only (from backpack)");
    ImGui::Separator();

    float totalDrain = 0.0f;

    // ---- 飛行物型 ----
    for (size_t i = 0; i < w.spells.size(); ++i)
    {
        const auto& s = w.spells[i];
        const ItemCommon* c = ItemDatabase::GetCommon(s.id);
        const char* name = c ? c->name : "Unknown";

        ImGui::PushID((int)i);
        ImGui::Text("[%d] %s", (int)i, name);
        ImGui::Indent();
        ImGui::Text("shots=%d  spread=%.0f  casts=%d  delay=%.2f",
            s.projectileCount, s.spreadAngle, s.castCount, s.castDelay);
        ImGui::Text("damage=%.1f  speed=%.1f  interval=%.2f  mana=%.1f",
            s.damage, s.speed, s.castInterval, s.manaCost);
        ImGui::Text("pending=%d  timer=%.2f", s.pendingCasts, s.castTimer);
        ImGui::Unindent();
        ImGui::PopID();

        if (s.castInterval > 0.0f)
            totalDrain += (s.manaCost * (float)s.castCount) / s.castInterval;
    }

    // ---- AOE 型（判定は GPU の Area。発動は WeaponSystem）----
    for (size_t i = 0; i < w.areas.size(); ++i)
    {
        const auto& a = w.areas[i];
        const ItemCommon* c = ItemDatabase::GetCommon(a.id);
        const char* name = c ? c->name : "Unknown";

        ImGui::PushID(1000 + (int)i);
        ImGui::Text("[AOE %d] %s", (int)i, name);
        ImGui::Indent();
        ImGui::Text("radius=%.1f  duration=%.1f  tick=%.2f  dmg/tick=%.1f",
            a.radius, a.duration, a.tickInterval, a.damagePerTick);
        ImGui::TextDisabled("cast by WeaponSystem, damage on GPU (profile #%d)", a.profile);
        ImGui::Unindent();
        ImGui::PopID();

        if (a.castInterval > 0.0f)
            totalDrain += a.manaCost / a.castInterval;
    }

    bool sustainable = totalDrain <= regen;
    ImGui::TextColored(sustainable ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
        "Total Drain %.1f/s  vs  Regen %.1f/s   %s",
        totalDrain, regen, sustainable ? "(sustainable)" : "(will run dry)");
}

// ============================================================
// ImGui: プレイヤー（状態機 + 能力値）
// 能力値は PlayerStatsComponent を直接いじる。シーンは持たない。
// ============================================================
void CollisionTestScene::DrawPlayerPanel()
{
    if (!ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!m_Registry.IsValid(m_Player)) return;

    // ---- 位置と速度 ----
    if (m_Registry.Has<TransformComponent>(m_Player) &&
        m_Registry.Has<RigidbodyComponent>(m_Player))
    {
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);

        ImGui::Text("Position : %.2f, %.2f, %.2f", tf.position.x, tf.position.y, tf.position.z);
        ImGui::Text("Velocity : %.2f, %.2f, %.2f", rb.velocity.x, rb.velocity.y, rb.velocity.z);
        ImGui::TextColored(rb.isGrounded ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.6f, 0.3f, 1),
            "Grounded : %s", rb.isGrounded ? "YES" : "no");

        ImGui::DragFloat3("Spawn Pos", m_SpawnPos, 0.1f);
        if (ImGui::Button("Reset to Spawn"))
        {
            tf.position = { m_SpawnPos[0], m_SpawnPos[1], m_SpawnPos[2] };
            rb.velocity = { 0, 0, 0 };
        }
    }

    // ---- 体力 ----
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        char buf[64];
        sprintf_s(buf, "%.0f / %.0f", hp.current, hp.max);
        ImGui::ProgressBar(hp.current / hp.max, ImVec2(-1, 0), buf);
    }

    // ---- 魔力 ----
    // 実行時に書くのは ManaSystem だけ。
    // ここで max / regen を触るのはデバッグ調整の例外（hp や stats と同じ扱い）。
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mana = m_Registry.Get<ManaComponent>(m_Player);
        char mbuf[64];
        sprintf_s(mbuf, "%.0f / %.0f", mana.current, mana.max);
        ImGui::ProgressBar((mana.max > 0.0f) ? mana.current / mana.max : 0.0f,
            ImVec2(-1, 0), mbuf);
        ImGui::DragFloat("Mana Max", &mana.max, 1.0f, 10.0f, 1000.0f);
        ImGui::DragFloat("Mana Regen", &mana.regen, 0.5f, 0.0f, 300.0f);
    }
    // ---- 経験値とレベル ----
    if (m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Level %d", lv.level);

        char ebuf[64];
        sprintf_s(ebuf, "%.0f / %.0f", lv.experience, lv.ExpToNext());
        ImGui::ProgressBar(lv.Progress(), ImVec2(-1, 0), ebuf);

        if (lv.IsChoosing())
            ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1),
                "Choosing a reward (%zu options)", lv.pendingChoices.size());

        ImGui::Text("Level ups : %d", m_LevelUpSystem.GetTotalLevelUps());
        ImGui::DragInt("Choice Count", &m_LevelUpSystem.choiceCount, 1, 1, 5);
        ImGui::DragFloat("Stat Card Weight", &m_LevelUpSystem.statWeight, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Exp Base", &lv.expBase, 5.0f, 10.0f, 1000.0f);
        ImGui::DragFloat("Exp / Level", &lv.expPerLevel, 1.0f, 0.0f, 500.0f);
        ImGui::DragFloat("Exp / Level^2", &lv.expPerLevelSq, 0.1f, 0.0f, 50.0f);

        if (ImGui::Button("+50 Exp")) lv.experience += 50.0f;
        ImGui::SameLine();
        if (ImGui::Button("Reset Level"))
        {
            lv.level = 1;
            lv.experience = 0.0f;
            lv.ClearChoices();
        }
    }

    // ---- 状態機（3層）----
    if (m_Registry.Has<PlayerStateComponent>(m_Player))
    {
        auto& state = m_Registry.Get<PlayerStateComponent>(m_Player);

        const char* moveNames[] = { "Idle", "Run", "Jump", "Fall", "Slide" };   // MoveStateID と同じ並び
        const char* actionNames[] = { "None", "Casting" };
        const char* dmgNames[] = { "Normal", "Hurt", "Dead" };

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "State Machine (3 layers)");
        ImGui::Text("Move   : %-6s  %.2fs", moveNames[(int)state.move], state.moveTime);
        ImGui::Text("Action : %-8s  %.2fs", actionNames[(int)state.action], state.actionTime);

        ImVec4 dcol = state.IsDead() ? ImVec4(1, 0.3f, 0.3f, 1)
            : (state.damage == DamageStateID::Hurt) ? ImVec4(1, 0.8f, 0.3f, 1)
            : ImVec4(0.4f, 1, 0.4f, 1);
        ImGui::TextColored(dcol, "Damage : %-7s  %.2fs",
            dmgNames[(int)state.damage], state.damageTime);

        if (state.IsInvincible())
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1, 1),
                "  invincible %.2fs", state.invincibleTimer);

        const uint32_t mask = state.SuppressMask();
        if (mask != Mask_None)
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "  suppressing: %s%s",
                (mask & Mask_Move) ? "Move " : "", (mask & Mask_Action) ? "Action" : "");

        ImGui::DragFloat("Hurt Duration", &state.hurtDuration, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Invincible Time", &state.invincibleAfterHit, 0.05f, 0.0f, 5.0f);

        if (ImGui::Button("Take 10 Damage"))
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, 10.0f);
        ImGui::SameLine();
        if (ImGui::Button("Revive"))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.current = hp.max;
            state.damage = DamageStateID::Normal;
            state.damageTime = 0.0f;
            state.invincibleTimer = 0.0f;
        }
    }

    // ---- アニメ（状態機 → クリップの写像の確認用）----
    if (m_Registry.Has<SkinnedAnimComponent>(m_Player))
    {
        auto& anim = m_Registry.Get<SkinnedAnimComponent>(m_Player);
        auto clipName = [&](const SkinnedAnimLayer& L) -> const char*
            {
                return (anim.model && L.clip >= 0) ? anim.model->GetClipName(L.clip).c_str() : "-";
            };

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Animation (3 layers)");
        ImGui::Text("base  : %-18s %.2fs", clipName(anim.base), anim.base.time);
        ImGui::Text("upper : %-18s %.2fs  w=%.2f%s", clipName(anim.upper), anim.upper.time,
            anim.upper.weight, anim.upper.finished ? " (end)" : "");
        ImGui::Text("over  : %-18s %.2fs  w=%.2f%s", clipName(anim.over), anim.over.time,
            anim.over.weight, anim.over.finished ? " (end)" : "");
        ImGui::Checkbox("Show Skinned", &anim.visible);
        ImGui::SameLine();
        ImGui::DragFloat("Yaw Offset", &anim.yawOffsetDeg, 1.0f, -180.0f, 180.0f);
        ImGui::DragFloat3("Model Offset", &anim.offset.x, 0.01f);
    }

    // ---- 能力値 ----
    if (m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Stats (PlayerStatsComponent)");

        bool dirty = false;
        dirty |= ImGui::DragFloat("Radius", &stats.radius, 0.01f, 0.05f, 3.0f);
        dirty |= ImGui::DragFloat("Height", &stats.height, 0.01f, 0.05f, 5.0f);
        dirty |= ImGui::ColorEdit3("Color", m_PlayerColor);
        if (dirty) RebuildPlayerMesh();

        // 無敵（手で遊んで確かめる時用。HP 0 でも死なない）
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            ImGui::Checkbox("God mode", &hp.invincible);
            ImGui::SameLine();
            ImGui::Text("HP %.0f / %.0f", hp.current, hp.max);
        }
        ImGui::DragFloat("HP Regen (/s)", &stats.healthRegen, 0.01f, 0.0f, 50.0f);
        ImGui::DragFloat("Move Speed", &stats.moveSpeed, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Jump Power", &stats.jumpPower, 0.1f, 0.0f, 30.0f);
        ImGui::DragInt("Extra Jumps", &stats.extraJumps, 0.1f, 0, 10);
        ImGui::DragFloat("Air Jump Falloff", &stats.airJumpFalloff, 0.01f, 0.0f, 1.0f);
        ImGui::Text("Jump CD : %.2f", stats.jumpCooldown);

        // ---- 滑り（左 Ctrl / パッド X）----
        if (ImGui::TreeNode("Slide"))
        {
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
            {
                const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
                const float hs = std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
                const float slopeDeg = DirectX::XMConvertToDegrees(std::acos((std::min)(1.0f, rb.groundNormal.y)));
                const bool sliding = m_Registry.Has<PlayerStateComponent>(m_Player)
                    && m_Registry.Get<PlayerStateComponent>(m_Player).slideActive;
                ImGui::Text("speed %.1f m/s  ground slope %.0f deg  %s", hs, slopeDeg, sliding ? "SLIDING" : "");
            }
            ImGui::DragFloat("Boost (x moveSpeed)", &stats.slideBoost, 0.02f, 1.0f, 4.0f);
            ImGui::DragFloat("Boost Cooldown (s)", &stats.slideBoostCooldown, 0.05f, 0.0f, 5.0f);
            ImGui::DragFloat("Friction (m/s2)", &stats.slideFriction, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Slope Gravity (m/s2)", &stats.slideGravity, 0.5f, 0.0f, 100.0f);
            ImGui::DragFloat("Max Speed (m/s)", &stats.slideMaxSpeed, 0.5f, 1.0f, 60.0f);
            ImGui::DragFloat("Min Speed (m/s)", &stats.slideMinSpeed, 0.1f, 0.0f, 20.0f);
            ImGui::DragFloat("Turn Rate (deg/s)", &stats.slideTurnRate, 1.0f, 0.0f, 720.0f);
            ImGui::DragFloat("Momentum Decay (m/s2)", &stats.momentumDecay, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Momentum Decay Air", &stats.momentumDecayAir, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Momentum Turn (deg/s)", &stats.momentumTurnRate, 1.0f, 0.0f, 1080.0f);
            ImGui::DragFloat("Lean (deg)", &m_PlayerAnimSystem.slideLeanDeg, 0.5f, -45.0f, 60.0f);
            ImGui::TreePop();
        }

        // ---- 歩様（Walk / Jog / Sprint と再生速度）----
        if (ImGui::TreeNode("Locomotion"))
        {
            auto& pa = m_PlayerAnimSystem;
            static const char* kGaitNames[] = { "Walk", "Jog", "Sprint" };
            ImGui::Text("gait %s  play rate %.2f", kGaitNames[std::clamp(pa.CurrentGait(), 0, 2)], pa.CurrentPlayRate());
            ImGui::DragFloat("Walk ref speed", &pa.walkRefSpeed, 0.01f, 0.1f, 10.0f);
            ImGui::DragFloat("Jog ref speed", &pa.jogRefSpeed, 0.01f, 0.1f, 15.0f);
            ImGui::DragFloat("Sprint ref speed", &pa.sprintRefSpeed, 0.01f, 0.1f, 20.0f);
            ImGui::DragFloat("Walk -> Jog (m/s)", &pa.walkToJog, 0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Jog -> Sprint (m/s)", &pa.jogToSprint, 0.05f, 0.0f, 20.0f);
            ImGui::DragFloat("Hysteresis (m/s)", &pa.gaitHysteresis, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Min play rate", &pa.minPlayRate, 0.01f, 0.1f, 1.0f);
            ImGui::DragFloat("Max play rate", &pa.maxPlayRate, 0.01f, 1.0f, 4.0f);

            // 向き（カメラの前から見た進む方向 → 体をそちらへ振り向かせる）
            ImGui::SeparatorText("Facing");
            if (m_Registry.Has<PlayerStateComponent>(m_Player))
            {
                const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
                ImGui::Text("move %s (%.0f deg from camera)", MoveDirName(st.moveDir), st.moveAngleCam);
            }
            ImGui::DragFloat("Face turn rate (deg/s)", &stats.faceTurnRate, 5.0f, 30.0f, 3000.0f);
            ImGui::TreePop();
        }

        // ---- 被弾のノックバック（距離は身位 = 体の幅単位）----
        if (ImGui::TreeNode("Knockback"))
        {
            const float body = stats.radius * 2.0f;
            ImGui::Text("body width %.2f m", body);
            ImGui::DragFloat("Melee (bodies)", &stats.knockMeleeBodies, 0.01f, 0.0f, 3.0f);
            ImGui::SameLine(); ImGui::TextDisabled("= %.2f m", stats.knockMeleeBodies * body);
            ImGui::DragFloat("Melee time (s)", &stats.knockMeleeTime, 0.005f, 0.02f, 1.0f);
            ImGui::DragFloat("Blast (bodies)", &stats.knockBlastBodies, 0.01f, 0.0f, 5.0f);
            ImGui::SameLine(); ImGui::TextDisabled("= %.2f m", stats.knockBlastBodies * body);
            ImGui::DragFloat("Blast time (s)", &stats.knockBlastTime, 0.005f, 0.02f, 1.0f);
            ImGui::DragFloat("Blast lift (m/s)", &stats.knockBlastLift, 0.1f, 0.0f, 15.0f);
            // 体の前から打たれた事にして後ろへ下げる（被弾はしない）
            const float yaw = DirectX::XMConvertToRadians(m_Registry.Get<TransformComponent>(m_Player).rotation.y);
            const Vector2 back(-std::sin(yaw), -std::cos(yaw));
            if (ImGui::Button("Test melee")) PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, back, false);
            ImGui::SameLine();
            if (ImGui::Button("Test blast")) PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, back, true);
            ImGui::TreePop();
        }
    }

    // 重力はプレイヤーの能力ではなく環境の値なのでシーンが持つ
    ImGui::Separator();
    ImGui::DragFloat("Gravity (scene)", &m_Gravity, 0.5f, -100.0f, 0.0f);
}

// ============================================================
// 衝突体のワイヤ描画
// ============================================================
void CollisionTestScene::DrawColliderDebug(Entity e, const Color& color)
{
    auto& tf = m_Registry.Get<TransformComponent>(e);
    auto& col = m_Registry.Get<ColliderComponent>(e);
    Vector3 c = tf.position + col.offset;

    auto& dbg = DebugManager::Get();
    switch (col.shape)
    {
    case ColliderShape::Sphere:  dbg.DrawWireSphere(c, col.radius, color); break;
    case ColliderShape::Capsule: dbg.DrawWireCapsule(c, col.radius, col.height, color); break;
    case ColliderShape::AABB:    dbg.DrawWireAABB(c, col.halfExtents, color); break;
    case ColliderShape::Convex:  dbg.DrawWireAABB(c, col.halfExtents, color); break;   // 包囲箱だけ
    }
}

// ============================================================
// 杖の可視化: 射程 / 標的 / 発射方向 / 待機中の発射
// ============================================================
void CollisionTestScene::DrawWandDebug()
{
    if (!m_Registry.IsValid(m_Player)) return;
    if (!m_Registry.Has<WandComponent>(m_Player)) return;

    auto& dbg = DebugManager::Get();
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& aim = m_WeaponSystem.GetAimDebug();

    Vector3 muzzle = tf.position + wand.muzzleOffset;
    dbg.DrawWireSphere(muzzle, wand.range, Color(0.25f, 0.4f, 0.8f, 1.0f));

    if (!aim.hasTarget) return;

    // 標的の位置
    {
        const Vector3 tp = aim.targetPos;
        const float m = 0.6f;
        const Color mark = aim.targetIsGpu ? Color(0.4f, 0.8f, 1.0f, 1.0f)   // 雑魚 = 水色
            : Color(1.0f, 1.0f, 0.2f, 1.0f);  // 精英 = 黄
        dbg.AddDebugLine(tp - Vector3(m, 0, 0), tp + Vector3(m, 0, 0), mark);
        dbg.AddDebugLine(tp - Vector3(0, m, 0), tp + Vector3(0, m, 0), mark);
        dbg.AddDebugLine(tp - Vector3(0, 0, m), tp + Vector3(0, 0, m), mark);
    }

    // 出力源ごとの発射方向（高さをずらして重ならないようにする）
    for (size_t i = 0; i < wand.spells.size(); ++i)
    {
        const auto& s = wand.spells[i];
        Vector3 origin = aim.muzzle + Vector3(0.0f, (float)i * 0.22f, 0.0f);

        Color col = (s.pendingCasts > 0) ? Color(1.0f, 0.6f, 0.2f, 1.0f)
            : Color(0.3f, 1.0f, 0.4f, 1.0f);

        int count = (s.projectileCount < 1) ? 1 : s.projectileCount;
        const float len = 3.0f;

        if (count == 1 || s.spreadAngle <= 0.0f)
        {
            dbg.AddDebugLine(origin, origin + aim.dir * len, col);
        }
        else
        {
            float step = s.spreadAngle / (float)(count - 1);
            float start = -s.spreadAngle * 0.5f;
            for (int k = 0; k < count; ++k)
            {
                float deg = start + step * (float)k;
                Matrix rot = Matrix::CreateRotationY(DirectX::XMConvertToRadians(deg));
                Vector3 d = Vector3::TransformNormal(aim.dir, rot);
                d.Normalize();
                dbg.AddDebugLine(origin, origin + d * len, col);
            }
        }

        // 待機中の二重釈放を短い縦線で数える
        for (int k = 0; k < s.pendingCasts; ++k)
        {
            Vector3 p = origin + aim.dir * 0.4f + Vector3(0.15f * (float)k, 0.0f, 0.0f);
            dbg.AddDebugLine(p, p + Vector3(0.0f, 0.18f, 0.0f), Color(1.0f, 0.5f, 0.1f, 1.0f));
        }
    }
}

void CollisionTestScene::DrawSwarmPanel()
{
    // ============================================================
    // GPU 側 gameplay（Swarm）
    // 位置や HP は GPU 上にしか無い。読めるのは counter だけ
    // ============================================================
    if (ImGui::CollapsingHeader("Swarm (GPU)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& c = m_Swarm.GetCounters();

        ImGui::Text("nearest  : dist %.2f  pos %.1f %.1f %.1f  key %08X",
            c.nearestDist, c.nearestPos[0], c.nearestPos[1], c.nearestPos[2], c.nearestKey);
        const auto& aim = m_WeaponSystem.GetAimDebug();
        ImGui::Text("aim      : %s  %s  range %.1f",
            aim.hasTarget ? "TARGET" : "none", aim.targetIsGpu ? "gpu" : "cpu", aim.range);

        ImGui::Text("alive proj    : %u", c.aliveProjectiles);
        ImGui::Text("alive enemy   : %u", c.aliveEnemies);
        ImGui::Text("kills (total) : %u", c.killCount);
        ImGui::Text("pending spawn : proj %d   enemy %d",
            m_Swarm.GetPendingProjSpawns(), m_Swarm.GetPendingEnemySpawns());
        ImGui::Text("sub steps     : %d   flush %.4f ms",
            m_Swarm.GetLastSubSteps(), m_Swarm.GetFlushMs());

        // ---- 雑魚の頭上の HP バー（見た目の調整）----
        if (ImGui::TreeNode("HP Bars"))
        {
            auto& bar = m_Swarm.hpBar;
            ImGui::Checkbox("Show", &bar.enabled);
            ImGui::DragFloat("Width (m)", &bar.width, 0.01f, 0.1f, 5.0f);
            ImGui::DragFloat("Height (m)", &bar.height, 0.005f, 0.02f, 1.0f);
            ImGui::DragFloat("Offset (m)", &bar.offset, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Border (m)", &bar.border, 0.001f, 0.0f, 0.1f);
            ImGui::ColorEdit4("Fill", &bar.fill.x);
            ImGui::ColorEdit4("Back", &bar.back.x);
            ImGui::ColorEdit4("Edge", &bar.edge.x);
            ImGui::TreePop();
        }

        // ---- 雑魚の足元の丸い影（太陽の影図には入れない代わり）----
        if (ImGui::TreeNode("Blob Shadows"))
        {
            auto& blob = m_Swarm.blobShadow;
            ImGui::Checkbox("Enabled##blob", &blob.enabled);
            ImGui::DragFloat("Radius (m)##blob", &blob.radius, 0.01f, 0.1f, 3.0f);
            ImGui::SliderFloat("Strength##blob", &blob.strength, 0.0f, 1.0f);
            ImGui::SliderFloat("Softness##blob", &blob.softness, 0.0f, 1.0f);
            ImGui::DragFloat("Lift (m)##blob", &blob.lift, 0.005f, 0.0f, 0.3f);
            ImGui::DragFloat("Height Clamp (m)##blob", &blob.clamp, 0.01f, 0.0f, 2.0f);
            ImGui::TreePop();
        }

        // ---- 隕石（DROP の弾）の着弾点の警告の輪 ----
        if (ImGui::TreeNode("Meteor Ring"))
        {
            auto& ring = m_Swarm.dropRing;
            ImGui::Checkbox("Enabled##dropRing", &ring.enabled);
            ImGui::DragFloat("Edge Width##dropRing", &ring.edgeWidth, 0.005f, 0.0f, 0.5f);
            ImGui::DragFloat("Lift##dropRing", &ring.lift, 0.005f, 0.0f, 0.3f);
            ImGui::ColorEdit4("Fill##dropRing", &ring.fill.x);
            ImGui::ColorEdit4("Edge##dropRing", &ring.edge.x);
            ImGui::ColorEdit4("Back##dropRing", &ring.back.x);
            ImGui::TreePop();
        }

        // ---- 経験値オーブの見た目（宝石の動き・光り方・吸い寄せ中の尾）----
        if (ImGui::TreeNode("Exp Orbs"))
        {
            auto& ol = m_Swarm.orbLook;
            ImGui::DragFloat("Scale##orb", &ol.scale, 0.01f, 0.1f, 5.0f);
            ImGui::DragFloat("Bob height (m)", &ol.bobHeight, 0.005f, 0.0f, 1.0f);
            ImGui::DragFloat("Bob speed", &ol.bobSpeed, 0.05f, 0.0f, 20.0f);
            ImGui::DragFloat("Spin speed", &ol.spinSpeed, 0.05f, -20.0f, 20.0f);
            ImGui::DragFloat("Pulse amount", &ol.pulseAmount, 0.005f, 0.0f, 0.5f);
            ImGui::DragFloat("Pulse speed", &ol.pulseSpeed, 0.05f, 0.0f, 30.0f);
            ImGui::ColorEdit3("Idle color", &ol.idleColor.x, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Pull color", &ol.pullColor.x, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Emissive", &ol.emissive, 0.02f, 0.0f, 10.0f);
            ImGui::SliderFloat("Facet", &ol.facet, 0.0f, 1.0f);
            ImGui::DragFloat("Rim gain", &ol.rimGain, 0.02f, 0.0f, 10.0f);
            ImGui::DragFloat("Rim power", &ol.rimPower, 0.05f, 0.1f, 16.0f);
            ImGui::ColorEdit3("Rim color", &ol.rimColor.x, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Glint gain", &ol.glintGain, 0.02f, 0.0f, 10.0f);
            ImGui::DragFloat("Glint power", &ol.glintPower, 0.5f, 1.0f, 256.0f);
            ImGui::SeparatorText("Pulled");
            ImGui::DragFloat("Full pull speed", &ol.fullPullSpeed, 0.1f, 0.1f, 50.0f);
            ImGui::DragFloat("Tilt max (rad)", &ol.tiltMax, 0.02f, 0.0f, 1.6f);
            ImGui::DragFloat("Stretch / speed", &ol.stretchPerSpeed, 0.002f, 0.0f, 0.5f);
            ImGui::DragFloat("Stretch max", &ol.stretchMax, 0.02f, 0.0f, 4.0f);
            ImGui::DragFloat("Pull glow", &ol.pullGlow, 0.02f, 0.0f, 10.0f);
            ImGui::Checkbox("Trail (ExpOrbTrail.json)", &ol.trail);
            ImGui::DragFloat("Trail min speed", &ol.trailMinSpeed, 0.1f, 0.0f, 50.0f);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Test Fire: alive proj should rise to ~100");
        ImGui::TextDisabled("then fall back to 0 within 3 seconds.");
        ImGui::Text("requested %u   dispatched %u   steps %u",
            m_Swarm.GetTotalRequested(), m_Swarm.GetTotalDispatched(), m_Swarm.GetTotalSteps());
        // ---- 範囲攻撃の確認：道具を持っていなくても、編集器のプロファイルを直接出せる ----
        ImGui::Text("alive areas %u   ticking %u   area vfx %zu",
            c.aliveAreas, c.tickingAreas, m_AreaVFX.GetActiveCount());
        if (AreaProfileDB::Count() > 1)
        {
            m_AreaTestProfile = (std::max)(1, (std::min)(m_AreaTestProfile, AreaProfileDB::Count() - 1));
            if (ImGui::BeginCombo("Area Profile", AreaProfileDB::At(m_AreaTestProfile).name.c_str()))
            {
                for (int i = 1; i < AreaProfileDB::Count(); ++i)
                    if (ImGui::Selectable(AreaProfileDB::At(i).name.c_str(), i == m_AreaTestProfile))
                        m_AreaTestProfile = i;
                ImGui::EndCombo();
            }

            const bool hasPlayer = m_Registry.IsValid(m_Player);
            auto castAt = [&](const Vector3& pos, bool atCaster)
                {
                    const AreaProfile& ap = AreaProfileDB::At(m_AreaTestProfile);
                    const Swarm::Area ar = ap.MakeArea(pos, atCaster);
                    m_Swarm.SpawnArea(ar);
                    m_AreaVFX.Play(ap.vfxFile, pos, ar.timeLeft,
                        (ar.flags & Swarm::kAreaFollowPlayer) != 0, m_VFXContext);
                };

            if (ImGui::Button("Cast at Nearest Enemy"))
            {
                Vector3 np, nv; float nd;
                if (m_Swarm.GetNearestEnemy(np, nv, nd)) castAt(np, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cast at Player") && hasPlayer)
                castAt(m_Registry.Get<TransformComponent>(m_Player).position, true);
        }
        else
        {
            ImGui::TextDisabled("no area profile yet (make one in the Projectile Editor, F4)");
        }

        // 生成経路が通っているかの最短確認。玩家の周りへ放射状に撃つ
        if (ImGui::Button("Test Fire 100"))
        {
            if (m_Registry.IsValid(m_Player))
            {
                const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position
                    + Vector3(0.0f, 1.0f, 0.0f);
                for (int i = 0; i < 100; ++i)
                {
                    const float a = 6.2831853f * (float)i / 100.0f;
                    Vector3 dir(std::cos(a), 0.0f, std::sin(a));
                    m_Swarm.SpawnProjectile(VFXId::Fireball, pp, dir * 20.0f, 10.0f, 0.25f, 3.0f);
                }
            }
        }
    }
}

void CollisionTestScene::DrawItemDatabasePanel()
{
    // ---------- Item Database ----------
    if (ImGui::CollapsingHeader("Item Database"))
    {
        for (ItemID id : ItemDatabase::GetAllIDs())
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!c) continue;

            const char* cat = "?";
            switch (c->category)
            {
            case ItemCategory::Projectile: cat = "Projectile"; break;
            case ItemCategory::Function:   cat = "Function";   break;
            case ItemCategory::Area:       cat = "Area";       break;
            case ItemCategory::Frame:      cat = "Frame";      break;
            case ItemCategory::Unknown:    cat = "Unknown";    break;
            }

            ImGui::TextColored(ImVec4(c->color.x, c->color.y, c->color.z, 1.0f),
                "%-14s [%s]  occupy=%zu  influence=%zu  icon=%s",
                c->name, cat, c->occupyCells.size(), c->influenceCells.size(),
                c->iconPath ? "yes" : "no");
        }
    }
}

void CollisionTestScene::DrawTerrainPanel()
{
    // ---------- 地形 ----------
    if (ImGui::CollapsingHeader("Terrain"))
    {
        ImGui::Text("Grid : %d x %d  (%.0fm x %.0fm)",
            m_Grid.Width(), m_Grid.Depth(),
            m_Grid.WorldWidth(), m_Grid.WorldDepth());

        auto& tc = m_TerrainConfig;
        int seed = (int)tc.seed;
        if (ImGui::InputInt("Seed", &seed)) tc.seed = (uint32_t)(seed < 0 ? 0 : seed);
        ImGui::SameLine();
        if (ImGui::Button("Random")) tc.seed = std::random_device{}() % 100000u;
        ImGui::DragInt("Plateaus", &tc.plateauCount, 0.2f, 0, 40);
        ImGui::DragIntRange2("Plateau Size", &tc.plateauMin, &tc.plateauMax, 0.2f, 3, 20);
        ImGui::DragFloatRange2("Plateau Height", &tc.heightMin, &tc.heightMax, 0.05f, 1.0f, 8.0f);
        ImGui::DragInt("Plateau Gap", &tc.gap, 0.1f, 1, 10);
        ImGui::SliderFloat("2nd Tier Chance", &tc.tier2Chance, 0.0f, 1.0f);
        ImGui::DragFloatRange2("2nd Tier Height", &tc.tier2HeightMin, &tc.tier2HeightMax, 0.05f, 1.0f, 6.0f);
        ImGui::DragInt("Ramp Width", &tc.rampWidth, 0.1f, 1, 5);
        ImGui::SliderFloat("Ramp Slope", &tc.rampSlopeDeg, 10.0f, 35.0f, "%.0f deg");
        ImGui::DragInt("Terraces", &tc.terraceCount, 0.1f, 0, 10);
        ImGui::DragFloatRange2("Terrace Height", &tc.terraceHeightMin, &tc.terraceHeightMax, 0.05f, 1.0f, 12.0f);
        ImGui::DragFloatRange2("Terrace Slope", &tc.terraceSlopeMin, &tc.terraceSlopeMax, 0.1f, 5.0f, 35.0f, "%.0f deg");
        ImGui::DragIntRange2("Terrace Top", &tc.terraceTopMin, &tc.terraceTopMax, 0.1f, 2, 16);
        ImGui::SliderFloat("Terrace 2nd Tier", &tc.terraceTier2Chance, 0.0f, 1.0f);
        ImGui::DragFloatRange2("Terrace 2nd Height", &tc.terraceTier2HeightMin, &tc.terraceTier2HeightMax, 0.05f, 1.0f, 8.0f);
        ImGui::DragInt("Terrace Clear", &tc.terraceClear, 0.1f, 1, 12);
        ImGui::DragInt("Spawn Clear", &tc.spawnClearRadius, 0.1f, 2, 20);
        ImGui::DragInt("Trees", &tc.treeCount, 1.0f, 0, 400);
        ImGui::DragInt("Rocks", &tc.rockCount, 1.0f, 0, 200);
        ImGui::DragInt("Bushes", &tc.bushCount, 1.0f, 0, 600);
        // これより小さい木・岩は見た目だけ（格子も衝突も無し）
        ImGui::DragFloat("Tree blocks if height >=", &tc.treeBlockMinHeight, 0.05f, 0.0f, 20.0f, "%.2f m");
        ImGui::DragFloat("Rock blocks if size >=", &tc.rockBlockMinSize, 0.05f, 0.0f, 10.0f, "%.2f m");
        ImGui::Checkbox("Rock Mountains (outer wall)", &tc.rockMountains);
        ImGui::DragFloat("Mountain Scale", &tc.mountainScale, 0.02f, 0.2f, 3.0f);

        if (ImGui::Button("Regenerate"))
        {
            // 古い地形を全部消して作り直す。
            // ※GPU 側は KillAll で全消し（雑魚・弾・オーブ）。
            //   counter は残るので撃破数などの累計は続く
            for (Entity e : m_Terrain)
                if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
            m_Terrain.clear();
            m_Grid.ClearAll();

            auto* device = Application::Get().GetGraphics().GetDevice();
            std::vector<uint8_t> grassMask;
            m_Torches.clear();
            TerrainGenerator::Generate(m_Registry, device, m_Grid, m_TerrainConfig, m_Terrain, &grassMask, &m_Torches);
            m_StaticProps.Build(m_Registry);   // 置物の instanced 表も作り直す
            m_Grass.Build(m_Grid, grassMask, m_TerrainConfig.seed, m_TerrainConfig.biome);   // 草の高さ・色・生やす所も

            // GPU 側の格子表も差し替える（古い表のままだと弾が壁を抜ける）
            m_Swarm.KillAll();
            m_Swarm.UploadTerrain(m_Grid);
            m_Swarm.BuildVFXTable();
            RespawnElites();
            RespawnCrates();   // 古い位置は新しい壁の中かもしれない
        }
        ImGui::SameLine();
        ImGui::TextDisabled("same seed = same map");

        // 木・岩・茂み・草の描画（モデル毎の instanced + 視錐台 / 距離の間引き）
        ImGui::Separator();
        m_StaticProps.DrawImGui();
    }
}

// ============================================================
// ImGui: Bloom（後処理）
// Graphics が持つ BloomParams をそのまま書き換える。
// 合成 PS は毎フレーム値を読むので、変えた瞬間に画面へ反映される。
// 粒子の加算が 1.0 を超えた分だけ光る（Threshold 以上）。
// ============================================================
void CollisionTestScene::DrawBloomPanel()
{
    if (!ImGui::CollapsingHeader("Bloom (Post Process)"))
        return;

    auto& bp = Application::Get().GetGraphics().GetBloomParams();

    // ---- 有効 / 無効 ----
    ImGui::Checkbox("Enabled", &bp.enabled);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset"))
        bp = BloomParams{};   // ヘッダの既定値へ戻す

    // ---- 抽出（BloomCS の prefilter）----
    ImGui::SeparatorText("Extract");
    ImGui::DragFloat("Threshold", &bp.threshold, 0.01f, 0.0f, 4.0f, "%.2f");
    ImGui::SetItemTooltip("HDR 1.0 = white. Only brightness above this goes into bloom");
    ImGui::DragFloat("Knee", &bp.knee, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Soft range below the threshold. 0 = hard cut");

    // ---- 合成（CompositePS）----
    ImGui::SeparatorText("Composite");
    ImGui::DragFloat("Intensity", &bp.intensity, 0.01f, 0.0f, 5.0f, "%.2f");
    ImGui::SetItemTooltip("scene + bloom * Intensity. 0 looks the same as disabled");
    ImGui::DragFloat("Exposure", &bp.exposure, 0.01f, 0.1f, 8.0f, "%.2f");
    ImGui::SetItemTooltip("Whole-screen brightness multiplier (applied after bloom is added)");
    ImGui::Checkbox("Tonemap (ACES)", &bp.tonemap);
    ImGui::SameLine();
    ImGui::Checkbox("Gamma (1/2.2)", &bp.gamma);

    // ---- 調整用の当たり値 ----
    ImGui::SeparatorText("Presets");
    if (ImGui::Button("Off"))
    {
        bp.enabled = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Soft"))
    {
        bp.enabled = true;
        bp.threshold = 1.2f; bp.knee = 0.6f; bp.intensity = 0.5f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Default"))
    {
        bp.enabled = true;
        bp.threshold = 1.0f; bp.knee = 0.5f; bp.intensity = 0.8f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Strong"))
    {
        bp.enabled = true;
        bp.threshold = 0.7f; bp.knee = 0.5f; bp.intensity = 1.6f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Isolate"))
    {
        // bloom だけを見る：閾値 0 で全部拾い、露出を落として bloom の形を確認する
        bp.enabled = true;
        bp.threshold = 0.0f; bp.knee = 0.0f; bp.intensity = 3.0f; bp.exposure = 0.3f;
    }
    ImGui::TextDisabled("Presets set Threshold / Knee / Intensity (Isolate also drops Exposure).");

    // ---- 現状の要約 ----
    ImGui::SeparatorText("State");
    ImGui::Text("Pipeline : resolve -> %s -> composite(%s%s)",
        bp.enabled ? "BloomCS x9 (prefilter + 4 down + 4 up)" : "(bloom skipped)",
        bp.tonemap ? "ACES" : "linear",
        bp.gamma ? ", gamma" : "");
    ImGui::Text("Effective bloom gain : %.2f", bp.enabled ? bp.intensity * bp.exposure : 0.0f);
}

// ============================================================
// 体格や色を変えた時に見た目を作り直す
// 数値は PlayerStatsComponent が持つので PlayerFactory に任せる
// ============================================================
void CollisionTestScene::RebuildPlayerMesh()
{
    auto* device = Application::Get().GetGraphics().GetDevice();
    PlayerFactory::RebuildVisual(m_Registry, m_Player, device,
        { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f });
}

// ============================================================
// TEMP-TEST: 戦闘の反応特効の自測（VFXL_BATTLE_AUTOTEST）
// ============================================================
void CollisionTestScene::AutoTestLog(const char* what)
{
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream f("autotest.log", std::ios::app);
    f << ms << " " << what << " (game " << m_AutoTime << " s)";
    const auto& cam = m_Camera.Camera();
    const Vector3 cp = cam.GetPosition();
    f << " cam dist " << cam.GetCurrentDistance() << (cam.IsOccluded() ? " occluded" : "")
      << " pitch " << cam.GetPitch() << " camPos " << cp.x << "," << cp.y << "," << cp.z;
    if (m_Registry.IsValid(m_Player))
    {
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        f << " player " << pp.x << "," << pp.y << "," << pp.z;
    }
    f << std::endl;
}

void CollisionTestScene::UpdateAutoTest(float dt)
{
    if (m_AutoUI || m_AutoChest) return;   // UpdateAutoTestUI / Chest が Update から回す
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_AutoBomber) { UpdateAutoTestBomber(); return; }
    if (m_AutoPerf) { UpdateAutoTestPerf(); return; }
    if (m_AutoSlide) { UpdateAutoTestSlide(dt); return; }
    if (m_AutoStress) { UpdateAutoTestStress(); return; }
    if (m_AutoMagnifier) { UpdateAutoTestMagnifier(); return; }
    if (m_AutoLoco) { UpdateAutoTestLoco(dt); return; }
    if (m_AutoBalance || m_AutoPickup) return;   // Update から回す
    if (m_AutoBoss) { UpdateAutoTestBoss(); return; }
    if (m_AutoAssets) { UpdateAutoTestAssets(); return; }
    if (m_AutoEdge) { UpdateAutoTestEdge(); return; }
    if (m_AutoArrow) { UpdateAutoTestArrow(); return; }
    if (m_AutoChain) { UpdateAutoTestChain(); return; }
    if (m_AutoBeam) { UpdateAutoTestBeam(); return; }
    if (m_AutoStuck) { UpdateAutoTestStuck(); return; }
    if (m_AutoGhost) { UpdateAutoTestGhost(); return; }
    if (m_AutoClip) { UpdateAutoTestClip(); return; }
    if (m_AutoKnock) { UpdateAutoTestKnock(); return; }
    if (m_AutoDrop) { UpdateAutoTestDrop(); return; }
    if (m_AutoBeamTrack) { UpdateAutoTestBeamTrack(); return; }
    if (m_AutoSurge) { UpdateAutoTestSurge(); return; }
    if (m_AutoPoison) { UpdateAutoTestPoison(); return; }

    if (m_AutoStep == 0 && m_AutoTime >= 5.0f && m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        lv.experience += lv.ExpToNext() + 1.0f;
        AutoTestLog("give exp");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 9.0f)
    {
        // 最寄りの箱の横（1.2m 手前）へ。高さはそのまま
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        Entity best = EntityTraits::NULL_ENTITY;
        float bestD = 1e9f;
        for (Entity c : m_Crates.GetCrates())
        {
            if (!m_Registry.IsValid(c) || !m_Registry.Has<InteractableComponent>(c)) continue;
            const float d = Vector3::DistanceSquared(m_Registry.Get<InteractableComponent>(c).basePos, tf.position);
            if (d < bestD) { bestD = d; best = c; }
        }
        if (best != EntityTraits::NULL_ENTITY)
        {
            const Vector3 cp = m_Registry.Get<InteractableComponent>(best).basePos;
            Vector3 dir = tf.position - cp;
            dir.y = 0.0f;
            if (dir.LengthSquared() < 1e-4f) dir = Vector3(1, 0, 0);
            dir.Normalize();
            tf.position = Vector3(cp.x + dir.x * 1.2f, tf.position.y, cp.z + dir.z * 1.2f);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            m_Camera.Camera().SnapToTarget();
            AutoTestLog("teleport to crate");
        }
        else
            AutoTestLog("no crate");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 10.0f)
    {
        m_AutoInteract = true;
        AutoTestLog("press F");
        m_AutoStep = 3;
    }
}

// 1 秒: 施法停止・湧き停止・GPU の雑魚を全部消して自爆兵 3 体（寄って来て点火 → 爆発するはず）
// 9 秒: 玩家に付いて動く小さな毒の輪（半径 1.5m、0.4 秒毎に 10）+ 自爆兵 3 体。
//       触れて点火した後、導火線（1 秒）の途中で 2 回目の tick に倒されるはず
//       （撃破数が増え、玩家への累計ダメージは増えない）。
//       同時に背包へ火球を置いて施法を戻す（MP が減って回復するかを毎秒の行で見る）
void CollisionTestScene::UpdateAutoTestBomber()
{
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;   // 画面の連写に調試の線を入れない
        m_Camera.Camera().SetPitch(50.0f);   // 足元（丸い影・警告の輪）が映るように見下ろす
        m_Camera.Camera().distance = 10.0f;
        m_Mobs.QueueDebugBombers(3);
        AutoTestLog("bomber A: casting paused, 3 bombers");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 9.0f)
    {
        Swarm::Area ring;
        ring.center = m_Registry.Get<TransformComponent>(m_Player).position;
        ring.radius = 1.5f;
        ring.damage = 10.0f;
        ring.tickInterval = 0.4f;
        ring.timeLeft = 8.0f;
        ring.flags = Swarm::kAreaFollowPlayer;
        m_Swarm.SpawnArea(ring);
        m_Mobs.QueueDebugBombers(3);
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int mid = BackpackComponent::GRID / 2;
            BackpackLogic::Place(bp, ItemID::Fireball, mid, mid, 0);
            bp.dirty = true;
        }
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        AutoTestLog("bomber B: damage ring r1.5 10/0.4s, 3 bombers");
        m_AutoStep = 2;
    }

    // 1 秒毎に平均 fps と MP（今 / 上限 / 予約中）
    static int s_Frames = 0;
    static float s_FpsTimer = 0.0f;
    ++s_Frames;
    s_FpsTimer += ImGui::GetIO().DeltaTime;
    if (s_FpsTimer >= 1.0f)
    {
        char fps[96];
        const ManaComponent* mp = m_Registry.Has<ManaComponent>(m_Player) ? &m_Registry.Get<ManaComponent>(m_Player) : nullptr;
        snprintf(fps, sizeof(fps), "fps %.1f mp %.1f/%.0f pending %.1f", s_Frames / s_FpsTimer,
            mp ? mp->current : -1.0f, mp ? mp->max : -1.0f, mp ? mp->pendingSpend : -1.0f);
        AutoTestLog(fps);
        s_Frames = 0;
        s_FpsTimer = 0.0f;
    }

    // counter（回読）と HP が変わった時だけ 1 行
    const auto& c = m_Swarm.GetCounters();
    const uint32_t now[4] = { c.aliveEnemies, c.killCount, c.playerDamage, c.aliveAreas };
    const float hp = m_Registry.Has<HealthComponent>(m_Player) ? m_Registry.Get<HealthComponent>(m_Player).current : 0.0f;
    if (std::equal(now, now + 4, m_AutoLast) && hp == m_AutoLastHp) return;
    std::copy(now, now + 4, m_AutoLast);
    m_AutoLastHp = hp;

    char line[160];
    snprintf(line, sizeof(line), "alive %u kills %u dmgTotal %.2f areas %u hp %.1f",
        now[0], now[1], now[2] / 100.0f, now[3], hp);
    AutoTestLog(line);
}

// ============================================================
// TEMP-TEST: 負荷の内訳（VFXL_BATTLE_AUTOTEST=perf）
// 4 秒の助走の後、8 秒毎に段を切り替え、各段の後ろ 6 秒の平均 fps・平均 / 最大フレーム ms・
// 雑魚の活き数を 1 行ずつ記録する。垂直同期は VFXL_NO_VSYNC で切っておく（上限で頭打ちになるので）。
// 升級の三択で止まらないよう経験値は毎フレーム 0 に戻す
// ============================================================
void CollisionTestScene::SetDecorPropsVisible(bool visible, int* outCount)
{
    // 野原の置物（木・石・灌木・草）は StaticPropRenderer がまとめて描いている
    m_StaticProps.GetSettings().enabled = visible;
    if (outCount) *outCount = m_StaticProps.GetStats().registered;
}

void CollisionTestScene::UpdateAutoTestPerf()
{
    // propCull: 0 = 既定の間引き / 1 = 視錐台だけ（距離で間引かない）/ 2 = 間引き無し（760 個全部）
    struct Phase { const char* name; bool hideProps; bool noDebug; int propCull; };
    static const Phase kPhases[] = {
        { "default", false, false, 0 },
        { "props hidden", true, false, 0 },
        { "default", false, false, 0 },
        { "debug draw off", false, true, 0 },
        { "props hidden + debug off", true, true, 0 },
        { "debug off, frustum cull only", false, true, 1 },
        { "debug off, no prop culling", false, true, 2 },
        { "default", false, false, 0 },
    };
    constexpr int   kPhaseCount = (int)(sizeof(kPhases) / sizeof(kPhases[0]));
    constexpr float kLead = 4.0f, kPhaseLen = 8.0f, kSettle = 2.0f;

    static bool   s_DebugDefault[3] = {};
    static StaticPropRenderer::Settings s_PropDefault;
    static int    s_Phase = -1;
    static bool   s_Done = false;
    static int    s_Frames = 0;
    static double s_SumMs = 0.0, s_MaxMs = 0.0, s_SumAlive = 0.0, s_SumProps = 0.0, s_SumSq = 0.0;
    static auto   s_Prev = std::chrono::steady_clock::now();

    const auto nowTime = std::chrono::steady_clock::now();
    const double frameMs = std::chrono::duration<double, std::milli>(nowTime - s_Prev).count();
    s_Prev = nowTime;

    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    char line[200];
    if (m_AutoStep == 0)
    {
        s_DebugDefault[0] = m_ShowWireframe;
        s_DebugDefault[1] = m_ShowWandDebug;
        s_DebugDefault[2] = m_ShowGridDebug;
        s_PropDefault = m_StaticProps.GetSettings();
        int models = 0;
        m_Registry.CreateView<TransformComponent, ModelComponent>()
            .Each([&](Entity, TransformComponent&, ModelComponent& mc) { if (mc.visible && mc.model && !mc.batched) ++models; });
        int decor = 0;
        SetDecorPropsVisible(true, &decor);
        snprintf(line, sizeof(line), "perf start: model entities %d + instanced props %d, colliders %d, vsync %s",
            models, decor, (int)m_CollisionSystem.GetWorldColliders().size(),
            GetEnvironmentVariableA("VFXL_NO_VSYNC", nullptr, 0) > 0 ? "off" : "on");
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (s_Done || m_AutoTime < kLead) return;

    const int phase = (int)((m_AutoTime - kLead) / kPhaseLen);
    if (phase != s_Phase)
    {
        // 前の段を締める
        if (s_Phase >= 0 && s_Frames > 0)
        {
            snprintf(line, sizeof(line), "phase %d [%s] fps %.1f avg %.2f ms sd %.2f ms max %.2f ms alive %.0f props drawn %.0f (%d frames)",
                s_Phase, kPhases[s_Phase].name, s_Frames * 1000.0 / s_SumMs, s_SumMs / s_Frames,
                std::sqrt((std::max)(0.0, s_SumSq / s_Frames - (s_SumMs / s_Frames) * (s_SumMs / s_Frames))), s_MaxMs,
                s_SumAlive / s_Frames, s_SumProps / s_Frames, s_Frames);
            AutoTestLog(line);
            // 段の最後 1 秒の CPU 内訳（FrameProfiler。1 フレームあたり ms）
            AutoTestLog(("  cpu " + FrameProfiler::Get().Summary()).c_str());
        }
        s_Frames = 0;
        s_SumMs = s_MaxMs = s_SumAlive = s_SumProps = s_SumSq = 0.0;
        s_Phase = phase;

        const bool done = phase >= kPhaseCount;
        const bool hideProps = !done && kPhases[phase].hideProps;
        const bool noDebug = !done && kPhases[phase].noDebug;
        const int propCull = done ? 0 : kPhases[phase].propCull;
        m_StaticProps.GetSettings() = s_PropDefault;
        if (propCull >= 1) m_StaticProps.GetSettings().distPerRadius = m_StaticProps.GetSettings().maxDistance = 0.0f;
        if (propCull >= 2) m_StaticProps.GetSettings().frustumCull = false;
        SetDecorPropsVisible(!hideProps, nullptr);
        m_ShowWireframe = !noDebug && s_DebugDefault[0];
        m_ShowWandDebug = !noDebug && s_DebugDefault[1];
        m_ShowGridDebug = !noDebug && s_DebugDefault[2];
        if (done)
        {
            AutoTestLog("perf done");
            s_Done = true;
        }
        return;
    }
    if (m_AutoTime - kLead - phase * kPhaseLen < kSettle) return;

    ++s_Frames;
    s_SumMs += frameMs;
    s_SumSq += frameMs * frameMs;
    s_MaxMs = (std::max)(s_MaxMs, frameMs);
    s_SumAlive += m_Swarm.GetCounters().aliveEnemies;
    s_SumProps += m_StaticProps.GetStats().drawn;
}

// ============================================================
// TEMP-TEST: 負荷試験（VFXL_BATTLE_AUTOTEST=stress）
// 玩家は動かず無敵（HP 1e6）、魔力も無限。開局の 3x3 枠へ火球・弧・追尾・隕石を置けるだけ置いて撃たせる。
// 湧きは SpawnDirector の上限を段ごとに上げ、10〜30m に一気に湧かせる（倒されても上限まで補充）。
// 段 12 秒、最初の 6 秒は寄って来るのを待つ。段の後半の平均 fps・最長フレーム・活き数と、
// 最後 1 秒の CPU / GPU の内訳（FrameProfiler）を記録。三択で止まらないよう経験値は毎フレーム 0
// ============================================================
void CollisionTestScene::UpdateAutoTestStress()
{
    struct Phase { const char* name; int mobs; int shots; };
    static const Phase kPhases[] = {
        { "0 mobs", 0, 0 },
        { "250 mobs", 250, 0 },
        { "500 mobs", 500, 0 },
        { "1000 mobs", 1000, 0 },
        { "2000 mobs", 2000, 0 },
        { "4000 mobs", 4000, 0 },
        { "4000 mobs + 2000 shots", 4000, 2000 },
    };
    constexpr int   kPhaseCount = (int)(sizeof(kPhases) / sizeof(kPhases[0]));
    constexpr float kLead = 3.0f, kPhaseLen = 12.0f, kSettle = 6.0f;

    static int    s_Phase = -1;
    static bool   s_Done = false;
    static int    s_Frames = 0;
    static double s_SumMs = 0.0, s_MaxMs = 0.0;
    static double s_Sum[4] = {};   // 雑魚 / 弾 / 経験値オーブ / 範囲
    static auto   s_Prev = std::chrono::steady_clock::now();

    const auto nowTime = std::chrono::steady_clock::now();
    const double frameMs = std::chrono::duration<double, std::milli>(nowTime - s_Prev).count();
    s_Prev = nowTime;

    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }

    char line[240];
    if (m_AutoStep == 0)
    {
        // 法術を開局の枠（3..5 行・列）へ置けるだけ置く
        int placed = 0;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const ItemID spells[] = { ItemID::Fireball, ItemID::ArcBolt, ItemID::HomingBolt, ItemID::Meteor };
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            for (ItemID id : spells)
            {
                bool done = false;
                for (int r = lo; r < lo + 3 && !done; ++r)
                    for (int c = lo; c < lo + 3 && !done; ++c)
                        if (BackpackLogic::CanPlace(bp, id, r, c, 0))
                        {
                            BackpackLogic::Place(bp, id, r, c, 0);
                            done = true;
                            ++placed;
                        }
            }
            bp.dirty = true;
        }
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        auto& d = m_Mobs.Director();
        d.enabled = true;
        d.spawnCap = 0;
        m_Mobs.scaling = false;   // 難度の倍率・湧く速さの曲線は切る（数だけ段毎に変える）
        d.spawnPerSecond = 2000.0f;
        d.maxPerFrame = 100;
        d.rMin = 10.0f;
        d.rMax = 30.0f;
        snprintf(line, sizeof(line), "stress start: %d spells placed, vsync %s", placed,
            GetEnvironmentVariableA("VFXL_NO_VSYNC", nullptr, 0) > 0 ? "off" : "on");
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (s_Done || m_AutoTime < kLead) return;

    const int phase = (int)((m_AutoTime - kLead) / kPhaseLen);
    if (phase != s_Phase)
    {
        if (s_Phase >= 0 && s_Frames > 0)
        {
            snprintf(line, sizeof(line),
                "stress %d [%s] fps %.1f avg %.2f ms max %.2f ms | mobs %.0f shots %.0f orbs %.0f areas %.0f (%d frames)",
                s_Phase, kPhases[s_Phase].name, s_Frames * 1000.0 / s_SumMs, s_SumMs / s_Frames, s_MaxMs,
                s_Sum[0] / s_Frames, s_Sum[1] / s_Frames, s_Sum[2] / s_Frames, s_Sum[3] / s_Frames, s_Frames);
            AutoTestLog(line);
            AutoTestLog(("  prof " + FrameProfiler::Get().Summary()).c_str());
        }
        s_Frames = 0;
        s_SumMs = s_MaxMs = 0.0;
        for (double& v : s_Sum) v = 0.0;
        s_Phase = phase;

        if (phase >= kPhaseCount)
        {
            m_Mobs.Director().spawnCap = 0;
            m_Stress.SetAutoRefill(false, 0, 0);
            AutoTestLog("stress done");
            s_Done = true;
            return;
        }
        m_Mobs.Director().spawnCap = kPhases[phase].mobs;
        m_Stress.SetAutoRefill(kPhases[phase].shots > 0, kPhases[phase].shots, 100);
        return;
    }
    if (m_AutoTime - kLead - phase * kPhaseLen < kSettle) return;

    const auto& c = m_Swarm.GetCounters();
    ++s_Frames;
    s_SumMs += frameMs;
    s_MaxMs = (std::max)(s_MaxMs, frameMs);
    s_Sum[0] += c.aliveEnemies;
    s_Sum[1] += c.aliveProjectiles;
    s_Sum[2] += c.aliveOrbs;
    s_Sum[3] += c.aliveAreas;
}

// ============================================================
// TEMP-TEST: 拡大鏡（VFXL_BATTLE_AUTOTEST=magnifier）
// 1 秒: 湧きを止めて正面 8〜9m に動かない的（HP 1000）を 3 体。
//       開局の 3x3 枠の左上に火球、右下に隕石（ここまでは素の大きさ）。魔力無限・経験値 0・見下ろし
// 9 秒: 中央に拡大鏡 → 斜めの 2 つが 1.5 倍になるはず（弾・爆発・隕石の警告の輪）
// 各段の 1 秒後に 集約後の法術（半径・消費）と 道具説明（DescribePlaced）を autotest.log へ。
// 1 秒毎に 弾・範囲の数。画面の見比べは外から連写（"magnifier A" / "magnifier B" の後）
// ============================================================
void CollisionTestScene::UpdateAutoTestMagnifier()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    const int lo = BackpackComponent::GRID / 2 - 1;

    auto logState = [&](const char* tag)
    {
        char line[240];
        for (const SpellStats& s : wand.spells)
        {
            const ItemCommon* ic = ItemDatabase::GetCommon(s.id);
            snprintf(line, sizeof(line), "%s spell %s radius %.3f mana %.2f damage %.1f",
                tag, ic ? ic->name : "?", s.radius, s.manaCost, s.damage);
            AutoTestLog(line);
        }
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, i);
            auto utf8 = [](const std::wstring& w)
            {
                const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
                std::string o(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
                return o;
            };
            std::string t = std::string(tag) + " sheet " + utf8(sh.title) + " :";
            for (const auto& tr : sh.traits) t += " [" + utf8(tr) + "]";
            for (const auto& l : sh.stats) t += " " + utf8(l.label) + "=" + utf8(l.value);
            AutoTestLog(t.c_str());
        }
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Camera.Camera().SetPitch(50.0f);
        m_Camera.Camera().distance = 14.0f;
        // 的は玩家の正面（鏡頭の奥）8〜9m に動かない雑魚 3 体。前後の段で同じ所に当たるので見比べやすい
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        const Vector3 targets[] = { { 0.0f, 0.0f, 8.0f }, { -1.5f, 0.0f, 9.0f }, { 1.5f, 0.0f, 9.0f } };
        for (const Vector3& t : targets)
            m_Swarm.SpawnEnemy(Vector3(pp.x + t.x, gy, pp.z + t.z), 1000.0f, 0.0f);
        ClearBackpackItems(bp);
        const int a = BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
        const int b = BackpackLogic::Place(bp, ItemID::Meteor, lo + 2, lo + 2, 0);
        bp.dirty = true;
        wand.castingPaused = false;
        char line[96];
        snprintf(line, sizeof(line), "magnifier A: fireball %d meteor %d (no magnifier)", a, b);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        logState("A");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 9.0f)
    {
        const int m = BackpackLogic::Place(bp, ItemID::Magnifier, lo + 1, lo + 1, 0);
        bp.dirty = true;
        char line[96];
        snprintf(line, sizeof(line), "magnifier B: magnifier %d placed in the middle", m);
        AutoTestLog(line);
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 10.0f)
    {
        logState("B");
        m_AutoStep = 4;
    }

    static float s_Timer = 0.0f;
    s_Timer += ImGui::GetIO().DeltaTime;
    if (s_Timer >= 1.0f)
    {
        s_Timer = 0.0f;
        const auto& c = m_Swarm.GetCounters();
        char line[96];
        snprintf(line, sizeof(line), "shots %u areas %u mobs %u", c.aliveProjectiles, c.aliveAreas, c.aliveEnemies);
        AutoTestLog(line);
    }
}

// ============================================================
// TEMP-TEST: 横 / 後ろ走りと爆発の見え方（VFXL_BATTLE_AUTOTEST=loco）
// 1 秒: 湧き停止・全消し・鏡頭を寄せる（5m、見下ろし 12 度）
// 2 秒〜: 鏡頭から見て 前 / 右 / 後 / 左 / 左後 / 右前 / 前（ゆっくり）へ 1.6 秒ずつ走る。
//         各段 0.9 秒で "loco <向き>" 行（脚の角度・後ろ走り・歩様・再生速度）→ 外から撮る
// その後: 鏡頭を既定に戻し、正面 8〜9m に動かない的 3 体 + 火球・隕石を背包へ。
//         "loco blast" 行を 2 回（外から撮る）→ "loco done"
// ============================================================
void CollisionTestScene::UpdateAutoTestLoco(float dt)
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();

    struct Seg { const char* name; float x, y; };   // 鏡頭基準の入力（x = 右、y = 前）
    static const Seg kSegs[] = {
        { "forward", 0.0f, 1.0f }, { "right", 1.0f, 0.0f }, { "back", 0.0f, -1.0f }, { "left", -1.0f, 0.0f },
        { "back-left", -0.7071f, -0.7071f }, { "forward-right", 0.7071f, 0.7071f }, { "forward-slow", 0.0f, 0.35f },
    };
    constexpr int kSegCount = (int)(sizeof(kSegs) / sizeof(kSegs[0]));
    constexpr float kSegTime = 1.6f;
    static float s_Phase = 0.0f;
    static bool  s_Logged = false;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        cam.distance = 5.0f;
        cam.SetPitch(12.0f);
        AutoTestLog("loco start");
        m_AutoStep = 1;
        s_Phase = 0.0f;
    }
    else if (m_AutoStep >= 1 && m_AutoStep <= kSegCount && m_AutoTime >= 2.0f)
    {
        const Seg& s = kSegs[m_AutoStep - 1];
        pcs.testInput = true;
        pcs.testMove = Vector2(s.x, s.y);
        pcs.testSlide = false;
        pcs.testJump = false;
        s_Phase += dt;
        if (!s_Logged && s_Phase >= 0.9f)
        {
            s_Logged = true;
            const auto& pa = m_PlayerAnimSystem;
            const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
            // 体の向きもカメラの前から測る（振り向き終わっていれば move の角度と同じになる）
            Vector3 cf = cam.GetForward(); cf.y = 0.0f;
            const float camYaw = DirectX::XMConvertToDegrees(std::atan2(cf.x, cf.z));
            const float bodyFromCam = std::remainder(camYaw - m_Registry.Get<TransformComponent>(m_Player).rotation.y, 360.0f);
            char line[200];
            snprintf(line, sizeof(line), "loco %s dir %s move %.0f body %.0f gait %d rate %.2f speed %.2f",
                s.name, MoveDirName(st.moveDir), st.moveAngleCam, bodyFromCam, pa.CurrentGait(), pa.CurrentPlayRate(),
                std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z));
            AutoTestLog(line);
        }
        if (s_Phase >= kSegTime)
        {
            s_Phase = 0.0f;
            s_Logged = false;
            ++m_AutoStep;
        }
    }
    else if (m_AutoStep == kSegCount + 1)
    {
        pcs.testInput = false;
        // 鏡頭は既定（Camera.json が無ければコードの既定）へ
        const FollowCamera def;
        cam.distance = def.distance;
        cam.ResetView();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        Vector3 f = cam.GetForward(); f.y = 0.0f; f.Normalize();
        const Vector3 r(f.z, 0.0f, -f.x);
        for (float side : { 0.0f, -1.5f, 1.5f })
        {
            const Vector3 t = pp + f * (side == 0.0f ? 8.0f : 9.0f) + r * side;
            m_Swarm.SpawnEnemy(Vector3(t.x, gy, t.z), 1000.0f, 0.0f);
        }
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            BackpackLogic::Place(bp, ItemID::Meteor, lo + 2, lo + 2, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        AutoTestLog("loco targets");
        s_Phase = 0.0f;
        m_AutoStep = kSegCount + 2;
    }
    else if (m_AutoStep == kSegCount + 2)
    {
        s_Phase += dt;
        if (s_Phase >= 4.0f && s_Phase - dt < 4.0f) AutoTestLog("loco blast");
        if (s_Phase >= 5.5f && s_Phase - dt < 5.5f) AutoTestLog("loco blast");
        if (s_Phase >= 7.0f) { AutoTestLog("loco done"); m_AutoStep = kSegCount + 3; }
    }
}

// ============================================================
// TEMP-TEST: 難度の推移（VFXL_BATTLE_AUTOTEST=balance）
// 実時間で数える（三択・背包の間は gameplay が止まるので Update から呼ぶ）。
// 0 秒: 無敵。5 秒毎に 1 行。10 秒: 経過時間を 170 秒へ（3:00 の精英）、30 秒: 300 秒へ、
// 45 秒: 600 秒へ（8:00 の精英・時間切れ → 最終波）、55 秒: 660 秒へ（最終波 3 段）。
// 出来事は "balance event"、その 5 秒後に "balance look"。70 秒で "balance done"
// ============================================================
void CollisionTestScene::UpdateAutoTestBalance(float dt)
{
    static float s_Real = 0.0f, s_Log = 0.0f;
    static int s_Jump = 0;
    s_Real += dt;
    if (!m_Registry.IsValid(m_Player)) return;

    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    hp.invincible = true;

    // 三択が出ていたら先頭を選ぶ
    if (m_Registry.Has<LevelComponent>(m_Player))
    {
        const auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        if (lv.IsChoosing())
        {
            const ItemID pick = lv.pendingChoices.front();
            LevelUpSystem::Choose(m_Registry, m_Player, pick);
            const ItemCommon* ic = ItemDatabase::GetCommon(pick);
            char line[128];
            snprintf(line, sizeof(line), "balance pick %s (level %d)", ic ? ic->name : "?", lv.level);
            AutoTestLog(line);
        }
    }

    if (s_Jump == 0 && s_Real >= 10.0f) { m_RunTime = 170.0f; s_Jump = 1; AutoTestLog("balance jump to 170 s"); }
    if (s_Jump == 1 && s_Real >= 30.0f) { m_RunTime = 300.0f; s_Jump = 2; AutoTestLog("balance jump to 300 s"); }
    if (s_Jump == 2 && s_Real >= 45.0f) { m_RunTime = 600.0f; s_Jump = 3; AutoTestLog("balance jump to 600 s"); }
    if (s_Jump == 3 && s_Real >= 55.0f) { m_RunTime = 660.0f; s_Jump = 4; AutoTestLog("balance jump to 660 s"); }

    // 時間で起きた出来事（精英など）。出てから 5 秒後にも 1 行（歩いて来たところを外から撮る）
    static float s_EventAt = -1.0f;
    if (const char* ev = m_Stage.ConsumeEvent())
    {
        char line[96];
        snprintf(line, sizeof(line), "balance event %s at run %.0f", ev, m_RunTime);
        AutoTestLog(line);
        s_EventAt = s_Real;
    }
    if (s_EventAt >= 0.0f && s_Real >= s_EventAt + 5.0f)
    {
        AutoTestLog("balance look");
        s_EventAt = -1.0f;
    }

    s_Log += dt;
    if (s_Log >= 5.0f)
    {
        s_Log = 0.0f;
        const auto& c = m_Swarm.GetCounters();
        const auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        char line[240];
        snprintf(line, sizeof(line),
            "balance real %.0f run %.0f mobs %u kills %u level %d exp %.0f/%.0f hp %.0f/%.0f statMul %.2f spawn/s %.2f contact %.1f orbs %u",
            s_Real, m_RunTime, c.aliveEnemies, c.killCount, lv.level, lv.experience, lv.ExpToNext(),
            hp.current, hp.max, m_Mobs.GetStatMul(), m_Mobs.Director().spawnPerSecond,
            m_Swarm.GetAIParams().contactDamage, c.aliveOrbs);
        AutoTestLog(line);
    }
    if (s_Real >= 70.0f && s_Jump == 4) { AutoTestLog("balance done"); s_Jump = 5; }
}

// ============================================================
// TEMP-TEST: 門 → Boss → クリア（VFXL_BATTLE_AUTOTEST=boss）
// 1 秒: 無敵・湧き停止・全消し、門の手前 2m へ移る。2 秒: F を押した扱い（Boss HP 300）。
// 毎秒 "boss t ..." 行。出来事は "boss event"。倒した後はシーンが「ステージクリア」→ リザルトへ
// ============================================================
void CollisionTestScene::UpdateAutoTestBoss()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;   // 三択で止めない
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Entity portal = m_Stage.GetPortal();
        if (m_Registry.IsValid(portal) && m_Registry.Has<InteractableComponent>(portal))
        {
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            const Vector3 pp = m_Registry.Get<InteractableComponent>(portal).basePos;
            Vector3 dir = tf.position - pp; dir.y = 0.0f;
            if (dir.LengthSquared() < 1e-4f) dir = Vector3(1, 0, 0);
            dir.Normalize();
            tf.position = Vector3(pp.x + dir.x * 2.0f, pp.y + 1.0f, pp.z + dir.z * 2.0f);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            m_Camera.Camera().SnapToTarget();
            AutoTestLog("boss at portal");
        }
        else
            AutoTestLog("boss: no portal");
        m_Stage.bossHp = 300.0f;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        m_AutoInteract = true;
        AutoTestLog("boss press F");
        m_AutoStep = 2;
    }

    if (const char* ev = m_Stage.ConsumeEvent())
    {
        char line[96];
        snprintf(line, sizeof(line), "boss event %s", ev);
        AutoTestLog(line);
    }

    // 倒した 2 秒後（「ステージクリア」の幕が出ている）に 1 行。外から撮る
    static float s_ClearedAt = -1.0f;
    if (m_AutoStep == 2 && m_Stage.IsCleared()) { s_ClearedAt = m_AutoTime; m_AutoStep = 3; }
    if (m_AutoStep == 3 && m_AutoTime >= s_ClearedAt + 2.0f) { AutoTestLog("boss done"); m_AutoStep = 4; }

    static float s_Log = 0.0f;
    s_Log += ImGui::GetIO().DeltaTime;
    if (m_AutoStep >= 2 && s_Log >= 1.0f)
    {
        s_Log = 0.0f;
        const auto& info = m_Swarm.GetBossInfo();
        char line[160];
        snprintf(line, sizeof(line), "boss t %.0f alive %u hp %.0f / %.0f ratio %.2f cleared %d",
            m_AutoTime, info.alive, Swarm::HpFromFixed(info.hp > info.maxHp ? 0u : info.hp),
            Swarm::HpFromFixed(info.maxHp), m_Stage.BossHpRatio(), m_Stage.IsCleared() ? 1 : 0);
        AutoTestLog(line);
    }
}

// ============================================================
// TEMP-TEST: 4 択・空中跳び・磁石（VFXL_BATTLE_AUTOTEST=pickup）
// 実時間。Update から呼ぶ（4 択の間は gameplay が止まる）。
// 1 秒: 升級の経験値 → 2 秒 "pickup cards N"（4 択の画面を撮る）→ 3 秒 選ぶ・空中 2 回にする
// 4.0 / 4.35 / 4.7 秒: 跳ぶ（地上 → 空中 → 空中）。押した次のフレームの vy を記録（12 → 9 → 6.75 のはず）
// 6〜16 秒: その場で撃たせて球を溜める。16 秒: 足元へ磁石を置いて乗る → 17 / 19 / 21 秒 球の数と経験値。22 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestPickup(float dt)
{
    static float s_Real = 0.0f;
    static int s_Step = 0;
    static int s_PendingJumpLog = 0;
    s_Real += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    char line[200];

    // 跳んだ次のフレーム: 上向きの速さを記録
    if (s_PendingJumpLog > 0)
    {
        snprintf(line, sizeof(line), "pickup jump #%d vy %.2f grounded %d airJumpsUsed %d",
            s_PendingJumpLog, rb.velocity.y, rb.isGrounded ? 1 : 0,
            m_Registry.Get<PlayerStateComponent>(m_Player).airJumpsUsed);
        AutoTestLog(line);
        s_PendingJumpLog = 0;
        pcs.testJump = false;
    }

    auto at = [&](float t) { return s_Real >= t && s_Real - dt < t; };

    if (s_Step == 0 && s_Real >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        lv.experience += lv.ExpToNext() + 1.0f;
        s_Step = 1;
    }
    else if (s_Step == 1 && s_Real >= 2.0f)
    {
        std::string names;
        for (ItemID id : lv.pendingChoices)
        {
            const ItemCommon* ic = ItemDatabase::GetCommon(id);
            names += std::string(" ") + (ic ? ic->name : "?");
        }
        snprintf(line, sizeof(line), "pickup cards %zu:%s", lv.pendingChoices.size(), names.c_str());
        AutoTestLog(line);
        s_Step = 2;
    }
    else if (s_Step == 2 && s_Real >= 3.0f)
    {
        if (lv.IsChoosing()) LevelUpSystem::Choose(m_Registry, m_Player, lv.pendingChoices.front());
        m_Registry.Get<PlayerStatsComponent>(m_Player).extraJumps = 2;
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        s_Step = 3;
    }
    else if (s_Step == 3)
    {
        lv.experience = 0.0f;   // 以降は三択で止めない
        int n = 0;
        if (at(4.0f)) n = 1;
        else if (at(4.35f)) n = 2;
        else if (at(4.7f)) n = 3;
        if (n > 0) { pcs.testJump = true; s_PendingJumpLog = n; }

        if (at(16.0f))
        {
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            snprintf(line, sizeof(line), "pickup before magnet: orbs %u level %d exp %.0f",
                m_Swarm.GetCounters().aliveOrbs, lv.level, m_ExpGained);
            AutoTestLog(line);
            m_Pickups.Place(m_Registry, m_Grid, pp, 1.5f, 2.5f);
            const auto ps = m_Pickups.GetPositions();
            if (!ps.empty())
            {
                // 置いた磁石（一番近い物）の上へ
                Vector3 best = ps.front();
                for (const Vector3& p : ps)
                    if (Vector3::DistanceSquared(p, pp) < Vector3::DistanceSquared(best, pp)) best = p;
                auto& tf = m_Registry.Get<TransformComponent>(m_Player);
                tf.position = Vector3(best.x, tf.position.y, best.z);
            }
        }
        if (at(17.0f) || at(19.0f) || at(21.0f))
        {
            snprintf(line, sizeof(line), "pickup after magnet: orbs %u level %d exp %.0f",
                m_Swarm.GetCounters().aliveOrbs, lv.level, m_ExpGained);
            AutoTestLog(line);
        }
        if (at(22.0f)) { AutoTestLog("pickup done"); s_Step = 4; }
    }
}

// ============================================================
// TEMP-TEST: 黄金の矢（VFXL_BATTLE_AUTOTEST=arrow）
// 1 秒: 湧き停止・全消し・無敵・MP 無限、正面 14m に動かない的 3 体、背包を黄金の矢だけにする。
//       鏡頭は 14m・見下ろし 15 度で、玩家の右 90 度から見る（矢が画面を横切る。VFXL_ARROW_FAR で 30m・40 度）
// 3 秒から 0.06 秒毎に "arrow look" を 16 回（外から連写）→ "arrow done"
// ============================================================
void CollisionTestScene::UpdateAutoTestArrow()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& cam = m_Camera.Camera();
    static int s_Shots = 0;
    static float s_Next = 3.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        // 的は玩家の +Z 側 14m（鏡頭の yaw 0 = +Z）
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -1.5f, 0.0f, 1.5f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 14.0f), 100000.0f, 0.0f);
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::GoldenArrow, lo + 1, lo + 1, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        cam.SetYaw(-90.0f);   // 右から（矢は画面を左 → 右へ横切る向き）
        // 弾道全体（玩家 → 14m 先の的）が画面に入る距離。近すぎると弾と一緒に飛ぶ矢が一瞬しか映らない
        cam.distance = 14.0f;
        cam.SetPitch(15.0f);
        if (GetEnvironmentVariableA("VFXL_ARROW_FAR", nullptr, 0) > 0) { cam.distance = 30.0f; cam.SetPitch(40.0f); }
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("arrow start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= s_Next)
    {
        const auto& c = m_Swarm.GetCounters();
        char line[96];
        snprintf(line, sizeof(line), "arrow look %d shots %u areas %u", s_Shots, c.aliveProjectiles, c.aliveAreas);
        AutoTestLog(line);
        s_Next += 0.06f;
        if (++s_Shots >= 16) { AutoTestLog("arrow done"); m_AutoStep = 2; }
    }
}

// ============================================================
// TEMP-TEST: 基礎魔法 → 高級魔法の誘発（VFXL_BATTLE_AUTOTEST=chain）
// 1 秒: 湧き停止・全消し・無敵・魔力無限。開局の 3x3 枠に 隕石（十字）を中央、火球を左上、石弾を左下
//       （どちらの上下左右も隕石の腕に掛かる）。正面 12m に動かない的を 3 体。横から見下ろし
// 1 秒毎に 届いた誘発の数・撃った隕石の数・弾と範囲の数を記録（隕石 1.8 秒毎に 1 個のはず）
// 7 秒: 石弾を外す → 隕石は目覚めていない（増えないはず）。10 秒: 石弾を戻す → 再開。13 秒 done
// 各段の頭で 杖の中身（triggered / triggerMask）と 隕石の説明（DescribePlaced の traits）も記録
// ============================================================
void CollisionTestScene::UpdateAutoTestChain()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    auto& cam = m_Camera.Camera();
    const int lo = BackpackComponent::GRID / 2 - 1;
    static float s_Next = 2.0f;

    auto utf8 = [](const std::wstring& w)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            std::string o(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
            return o;
        };
    auto logState = [&](const char* tag)
        {
            std::string t = std::string(tag) + " wand:";
            for (const auto& s : wand.spells)
            {
                char b[96];
                snprintf(b, sizeof(b), " [id %d trig %d mask %u cd %.2f]", (int)s.id, s.triggered ? 1 : 0, s.triggerMask, s.castInterval);
                t += b;
            }
            AutoTestLog(t.c_str());
            for (int i = 0; i < (int)bp.items.size(); ++i)
            {
                const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, i);
                std::string l = std::string(tag) + " sheet " + utf8(sh.title) + " :";
                for (const auto& tr : sh.traits) l += " [" + utf8(tr) + "]";
                AutoTestLog(l.c_str());
            }
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -2.0f, 0.0f, 2.0f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 12.0f), 100000.0f, 0.0f);

        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Meteor, lo + 1, lo + 1, 0);
        BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
        BackpackLogic::Place(bp, ItemID::StoneShot, lo + 2, lo, 0);
        bp.dirty = true;
        wand.castingPaused = false;

        // 背後から的（+Z 12m）を画面の真ん中に。横からだと窓が画面より大きい時に的が外へ出る
        cam.SetYaw(0.0f);
        cam.distance = 14.0f;
        cam.SetPitch(40.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("chain start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep >= 1 && m_AutoStep <= 3 && m_AutoTime >= s_Next)
    {
        if (s_Next == 2.0f || s_Next == 8.0f || s_Next == 11.0f) logState(m_AutoStep == 2 ? "chain off" : "chain on");
        const auto& c = m_Swarm.GetCounters();
        char line[128];
        snprintf(line, sizeof(line), "chain t %.0f events %u meteors %u proj %u areas %u",
            s_Next, m_WeaponSystem.GetTriggerEventsSeen(), m_WeaponSystem.GetTriggeredCasts(),
            c.aliveProjectiles, c.aliveAreas);
        AutoTestLog(line);
        s_Next += 1.0f;

        if (m_AutoStep == 1 && m_AutoTime >= 7.0f)
        {
            // 石弾（左下）を外す → 隕石は火球だけ = 目覚めない
            for (int i = 0; i < (int)bp.items.size(); ++i)
                if (bp.items[i].id == ItemID::StoneShot) { BackpackLogic::Remove(bp, i); break; }
            bp.dirty = true;
            m_AutoStep = 2;
        }
        else if (m_AutoStep == 2 && m_AutoTime >= 10.0f)
        {
            BackpackLogic::Place(bp, ItemID::StoneShot, lo + 2, lo, 0);
            bp.dirty = true;
            m_AutoStep = 3;
        }
        else if (m_AutoStep == 3 && m_AutoTime >= 13.0f)
        {
            AutoTestLog("chain done");
            // 画面の確認用: 石弾をもう一度外して背包と隕石の説明を開く（暗く沈んだ隕石 +「未発動」）。
            // 背包を開くと gameplay が止まってここへ来なくなるので、これが最後
            int meteorIdx = -1;
            for (int i = (int)bp.items.size() - 1; i >= 0; --i)
                if (bp.items[i].id == ItemID::StoneShot) BackpackLogic::Remove(bp, i);
            for (int i = 0; i < (int)bp.items.size(); ++i)
                if (bp.items[i].id == ItemID::Meteor) meteorIdx = i;
            bp.dirty = true;
            m_GameUI.TestShow(1);
            m_GameUI.TestTooltip(meteorIdx, { m_ScreenW * 0.62f, m_ScreenH * 0.30f });
            AutoTestLog("chain look backpack");
            m_AutoStep = 4;
        }
    }
}

// ============================================================
// TEMP-TEST: 外周の岩山（VFXL_BATTLE_AUTOTEST=edge）
// 1 秒: 湧き停止・全消し・無敵、北の縁から 15m の所へ移り縁を向く（鏡頭 10m・見下ろし 8 度）→ 3 秒 "edge look near"
// → 鏡頭 60m・見下ろし 45 度 → 5.5 秒 "edge look high" → 場地の中央・既定の鏡頭 → 8 秒 "edge look center"（fps も）→ 9 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestEdge()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    auto place = [&](float x, float z)
        {
            tf.position = Vector3(x, m_Grid.SampleHeight(x, z) + 1.0f, z);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        place(0.0f, m_Grid.WorldDepth() * 0.5f - 15.0f);
        cam.SetYaw(0.0f);   // yaw 0 = +Z（北の縁）を向く（FollowCamera: forward = (-sin, ., cos)）
        cam.distance = 10.0f;
        cam.SetPitch(8.0f);
        cam.avoidOcclusion = false;   // 後ろの台地で寄らないように
        cam.SnapToTarget();
        m_AutoStep = 1;
    }
    // 撮影は記録の後に外から非同期で行うので、記録してから 0.6 秒は鏡頭を動かさない
    else if (m_AutoStep == 1 && m_AutoTime >= 3.0f) { AutoTestLog("edge look near"); m_AutoStep = 11; }
    else if (m_AutoStep == 11 && m_AutoTime >= 3.6f)
    {
        cam.distance = 60.0f;
        cam.SetPitch(45.0f);
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 5.5f) { AutoTestLog("edge look high"); m_AutoStep = 12; }
    else if (m_AutoStep == 12 && m_AutoTime >= 6.1f)
    {
        cam.avoidOcclusion = true;
        const FollowCamera def;
        cam.distance = def.distance;
        cam.ResetView();
        cam.SetYaw(0.0f);
        place(0.0f, 0.0f);
        cam.SnapToTarget();
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 8.0f)
    {
        char line[96];
        snprintf(line, sizeof(line), "edge look center fps %.0f", ImGui::GetIO().Framerate);
        AutoTestLog(line);
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 9.0f)
    {
        AutoTestLog("edge done");
        m_AutoStep = 5;
    }
}

// ============================================================
// TEMP-TEST: 新しい素材の並べ見（VFXL_BATTLE_AUTOTEST=assets）
// 1 秒: 湧き停止・全消し・無敵。2 / 7 / 12 秒: 1 包ずつ（前の包は消す）玩家の前へ格子に並べ、
// 模型毎の大きさ（ファイルの単位を掛けた m）を記録。各 3 秒後に "assets look <包>"（外から撮る）。
// VFXL_ASSET_SET=rocks なら岩の 3 組（Rock-Set / KayKit Forest の Rock_ / dglopez の *rock*）を並べた後、
// 各組の岩を 8〜20m に拡大して 2 列に積んだ「山の壁」を 3 本並べて撮る（"assets look wall" / "wall top"）
// ============================================================
void CollisionTestScene::UpdateAutoTestAssets()
{
    struct Pack { const char* dir; const char* contains; };   // contains: 名前にこれを含む物だけ（小文字比較、空 = 全部）
    static const Pack kDefault[] = {
        { "Assets/Model/dglopez_WesternDesert/FBX", "" },
        { "Assets/Model/Quaternius_UltimateNature/FBX", "" },
        { "Assets/Model/Quaternius_ModularRuins/FBX", "" },
    };
    static const Pack kRocks[] = {
        { "Assets/Model/Rock-Set", "" },
        { "Assets/Model/KayKit_Forest/fbx", "rock_" },
        { "Assets/Model/dglopez_WesternDesert/FBX", "rock" },
    };
    static const Pack kArrows[] = {
        { "Assets/VFX/Mesh", "gongjian" },
        { "Assets/VFX/Mesh", "gonjian" },
        { "Assets/VFX/Mesh", "jian0" },
    };
    static const std::string s_Set = [] {
        char v[16] = {};
        return GetEnvironmentVariableA("VFXL_ASSET_SET", v, sizeof(v)) > 0 ? std::string(v) : std::string();
    }();
    static const bool s_Rocks = (s_Set == "rocks");
    const Pack* kPacks = s_Rocks ? kRocks : (s_Set == "arrows" ? kArrows : kDefault);
    const int kPackCount = 3;
    static std::vector<std::shared_ptr<Model>> s_PackModels[3];   // 山の壁に使う（組毎）

    auto listFiles = [](const Pack& p)
        {
            std::vector<std::string> files;
            for (const auto& de : std::filesystem::recursive_directory_iterator(p.dir))
            {
                std::string ext = de.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (ext != ".fbx") continue;
                std::string name = de.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (*p.contains && name.find(p.contains) == std::string::npos) continue;
                files.push_back(de.path().generic_string());
            }
            std::sort(files.begin(), files.end());
            return files;
        };
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        auto& cam = m_Camera.Camera();
        cam.distance = 14.0f;
        cam.SetPitch(25.0f);
        m_AutoStep = 1;
        return;
    }
    const int pack = m_AutoStep - 1;   // 0.. 並べる包
    if (m_AutoStep >= 1 && m_AutoStep <= kPackCount && m_AutoTime >= 2.0f + 5.0f * pack)
    {
        for (Entity e : m_AutoAssetEntities) if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
        m_AutoAssetEntities.clear();

        const std::vector<std::string> files = listFiles(kPacks[pack]);
        s_PackModels[pack].clear();

        // 玩家の前（鏡頭の奥）に 列 cols で並べる。間隔は大きい物に合わせて広め
        const auto& cam = m_Camera.Camera();
        Vector3 f = cam.GetForward(); f.y = 0.0f; f.Normalize();
        const Vector3 r(f.z, 0.0f, -f.x);
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const int cols = (int)std::ceil(std::sqrt((float)files.size() * 1.6f));
        const float gap = (pack == 2) ? 5.0f : 3.0f;
        char line[256];
        for (int i = 0; i < (int)files.size(); ++i)
        {
            auto m = ResourceManager::Get().LoadModel(files[i]);
            if (!m) { snprintf(line, sizeof(line), "assets FAIL %s", files[i].c_str()); AutoTestLog(line); continue; }
            const float u = m->GetFileUnitScale();
            const Vector3 size = (m->GetBoundsMax() - m->GetBoundsMin()) * u;
            if (size.y > 0.4f) s_PackModels[pack].push_back(m);   // 小石は山に使わない
            snprintf(line, sizeof(line), "assets size %s unit %.3f  %.2f x %.2f x %.2f m",
                std::filesystem::path(files[i]).filename().string().c_str(), u, size.x, size.z, size.y);
            AutoTestLog(line);

            const int cx = i % cols, cz = i / cols;
            Vector3 pos = pp + f * (6.0f + cz * gap) + r * ((cx - (cols - 1) * 0.5f) * gap);
            pos.y = m_Grid.SampleHeight(pos.x, pos.z) - m->GetBoundsMin().y * u;
            Entity e = m_Registry.Create();
            TransformComponent tf;
            tf.position = pos;
            tf.scale = { u, u, u };
            m_Registry.Add<TransformComponent>(e, tf);
            ModelComponent mc;
            mc.model = m;
            m_Registry.Add<ModelComponent>(e, mc);
            m_AutoAssetEntities.push_back(e);
        }
        snprintf(line, sizeof(line), "assets pack %s: %d models", kPacks[pack].dir, (int)files.size());
        AutoTestLog(line);
        m_AutoStep += 100;   // 撮るのを待つ（下で戻す）
    }
    else if (m_AutoStep > 100 && m_AutoStep < 200)
    {
        const int p = m_AutoStep - 101;
        if (m_AutoTime >= 5.0f + 5.0f * p)
        {
            char line[128];
            snprintf(line, sizeof(line), "assets look %d", p);
            AutoTestLog(line);
            m_AutoStep = p + 2;
            if (m_AutoStep > kPackCount)
            {
                if (s_Rocks) m_AutoStep = 500;   // 山の壁へ
                else { AutoTestLog("assets done"); m_AutoStep = 999; }
            }
        }
    }
    else if (m_AutoStep == 500)
    {
        // ---- 山の壁: 組毎に 36m、手前の列 8〜12m・奥の列 14〜20m に拡大して隙間なく積む ----
        for (Entity e : m_AutoAssetEntities) if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
        m_AutoAssetEntities.clear();
        auto& cam = m_Camera.Camera();
        Vector3 f = cam.GetForward(); f.y = 0.0f; f.Normalize();
        const Vector3 r(f.z, 0.0f, -f.x);
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        std::mt19937 rng(1234u);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        char line[160];
        for (int pk = 0; pk < kPackCount; ++pk)
        {
            const auto& models = s_PackModels[pk];
            if (models.empty()) continue;
            const float segCenter = (pk - 1) * 42.0f;
            for (int row = 0; row < 2; ++row)
            {
                const float step = row == 0 ? 5.0f : 7.0f;
                const float hMin = row == 0 ? 8.0f : 14.0f, hMax = row == 0 ? 12.0f : 20.0f;
                for (float x = -18.0f; x <= 18.0f; x += step)
                {
                    const auto& m = models[(size_t)(u01(rng) * models.size()) % models.size()];
                    const float unit = m->GetFileUnitScale();
                    const Vector3 lo = m->GetBoundsMin() * unit, hi = m->GetBoundsMax() * unit;
                    const float targetH = hMin + (hMax - hMin) * u01(rng);
                    const float s = targetH / (std::max)(hi.y - lo.y, 0.1f);
                    Vector3 pos = pp + f * (16.0f + row * 7.0f + u01(rng) * 2.0f) + r * (segCenter + x + u01(rng) * 2.0f);
                    pos.y = m_Grid.SampleHeight(pos.x, pos.z) - lo.y * s - targetH * 0.08f;   // 少し埋める（浮いて見えない）
                    Entity e = m_Registry.Create();
                    TransformComponent tf;
                    tf.position = pos;
                    tf.rotation = { 0.0f, u01(rng) * 360.0f, 0.0f };
                    tf.scale = { unit * s, unit * s, unit * s };
                    m_Registry.Add<TransformComponent>(e, tf);
                    ModelComponent mc;
                    mc.model = m;
                    m_Registry.Add<ModelComponent>(e, mc);
                    m_AutoAssetEntities.push_back(e);
                }
            }
            snprintf(line, sizeof(line), "assets wall %d (%s): %d rock models, left -> right", pk, kPacks[pk].dir, (int)models.size());
            AutoTestLog(line);
        }
        cam.distance = 12.0f;
        cam.SetPitch(4.0f);
        m_AutoStep = 501;
    }
    else if (m_AutoStep == 501 && m_AutoTime >= 20.0f)
    {
        AutoTestLog("assets look wall");
        m_Camera.Camera().distance = 45.0f;
        m_Camera.Camera().SetPitch(35.0f);
        m_AutoStep = 502;
    }
    else if (m_AutoStep == 502 && m_AutoTime >= 22.5f)
    {
        AutoTestLog("assets look walltop");
        m_AutoStep = 503;
    }
    else if (m_AutoStep == 503 && m_AutoTime >= 23.0f)
    {
        AutoTestLog("assets done");
        m_AutoStep = 999;
    }
}

// ============================================================
// TEMP-TEST: 幻想 UI の見た目（VFXL_BATTLE_AUTOTEST=ui）
// 1 秒: 湧き停止、背包へ火球・隕石・拡大鏡・分裂のルーンを置く（MP 無限、経験値 0）
// 2 秒: 背包を開く "ui backpack" / 4 秒: 拡大鏡の tooltip "ui tooltip" / 6 秒: 一時停止 "ui pause"
// 8 秒: 全部閉じる "ui hud" / 10 秒: 経験値を渡して三択 "ui levelup" / 13 秒 "ui done"
// 背包・一時停止の間は gameplay が止まるので Update から呼ぶ（dt は止まっていても進む）
// ============================================================
void CollisionTestScene::UpdateAutoTestUI(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (m_AutoStep < 5 && m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    static int s_MagnifierIndex = -1;
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            BackpackLogic::Place(bp, ItemID::Meteor, lo + 2, lo + 2, 0);
            s_MagnifierIndex = BackpackLogic::Place(bp, ItemID::Magnifier, lo + 1, lo + 1, 0);
            BackpackLogic::Place(bp, ItemID::SplitRune, lo, lo + 2, 0);
            bp.dirty = true;
        }
        // 魔法書（背包の横の箱）にも置いていない物を入れておく
        if (m_Registry.Has<SpellbookComponent>(m_Player))
        {
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            book.Learn(ItemID::ArcBolt);
            book.Learn(ItemID::HomingBolt);
            book.Learn(ItemID::DoubleCastRune);
            book.Learn(ItemID::Magnifier);
        }
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        // カメラの調整値の保存 → 変更 → 読込 が往復するか（本物の Camera.json には触らない）
        {
            auto& cam = m_Camera.Camera();
            const float before = cam.fov;
            m_Camera.SaveSettings("autotest_camera.json");
            cam.fov = 70.0f;
            m_Camera.LoadSettings("autotest_camera.json");
            char line[128];
            snprintf(line, sizeof(line), "ui camera settings round trip: fov %.1f -> 70 -> %.1f", before, cam.fov);
            AutoTestLog(line);
        }
        AutoTestLog("ui items placed");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        m_GameUI.TestShow(1);
        AutoTestLog("ui backpack");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 4.0f)
    {
        m_GameUI.TestTooltip(s_MagnifierIndex, { m_ScreenW * 0.62f, m_ScreenH * 0.30f });
        AutoTestLog("ui tooltip");
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 6.0f)
    {
        m_GameUI.TestTooltip(-1, { 0.0f, 0.0f });
        m_GameUI.TestShow(2);
        AutoTestLog("ui pause");
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 8.0f)
    {
        m_GameUI.TestShow(0);
        AutoTestLog("ui hud");
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5 && m_AutoTime >= 10.0f && m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        lv.experience += lv.ExpToNext() + 1.0f;
        AutoTestLog("ui levelup");
        m_AutoStep = 6;
    }
    else if (m_AutoStep == 6 && m_AutoTime >= 10.5f && m_Registry.Has<LevelComponent>(m_Player))
    {
        // 三択の中身を新しい能力値のカードに差し替える（見た目の確認用）
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        if (lv.IsChoosing())
        {
            lv.pendingChoices = { ItemID::MoveSpeedUp, ItemID::JumpPowerUp, ItemID::MaxHealthUp };
            AutoTestLog("ui levelup choices: MoveSpeedUp / JumpPowerUp / MaxHealthUp");
        }
        m_AutoStep = 7;
    }
    else if (m_AutoStep == 7 && m_AutoTime >= 12.0f && m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        // 実際に選んで、速さと跳ぶ力が上がるかを記録する
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        auto& st = m_Registry.Get<PlayerStatsComponent>(m_Player);
        char line[160];
        snprintf(line, sizeof(line), "ui stat before: moveSpeed %.3f jumpPower %.3f", st.moveSpeed, st.jumpPower);
        AutoTestLog(line);
        LevelUpSystem::Choose(m_Registry, m_Player, ItemID::MoveSpeedUp);
        lv.pendingChoices = { ItemID::JumpPowerUp };
        LevelUpSystem::Choose(m_Registry, m_Player, ItemID::JumpPowerUp);
        snprintf(line, sizeof(line), "ui stat after : moveSpeed %.3f jumpPower %.3f", st.moveSpeed, st.jumpPower);
        AutoTestLog(line);
        m_AutoStep = 8;
    }
    else if (m_AutoStep == 8 && m_AutoTime >= 13.0f)
    {
        AutoTestLog("ui done");
        m_AutoStep = 9;
    }
}

// ============================================================
// TEMP-TEST: 滑りの自測（VFXL_BATTLE_AUTOTEST=slide）
// 2 秒: 一番長い下り坂の上へ（高さ図を 1m 刻みで見て、連続して下る距離が一番長い所）。
//       0.4 秒走ってから滑る（坂で速くなるはず）→ 2.4 秒で跳ぶ（水平の速さが残るはず）
// 次: 平地（進む先 12m が同じ高さ）へ。0.4 秒走ってから滑る（押し出し → 摩擦で減って立つ）
// 0.1 秒毎に 水平の速さ・足元の傾き・接地・滑り中か・高さ を autotest.log へ
// ============================================================
void CollisionTestScene::UpdateAutoTestSlide(float dt)
{
    static Vector3 s_Dir(0.0f, 0.0f, 1.0f);
    static Vector3 s_SlopeStart, s_SlopeDir(0.0f, 0.0f, 1.0f);   // 1) で見つけた坂（最後の全景用）
    static float s_Phase = 0.0f;
    static float s_Log = 0.0f;
    auto& pcs = m_PlayerControlSystem;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;   // 三択で止めない

    const int W = m_Grid.Width(), D = m_Grid.Depth();
    auto walkableAt = [&](const Vector3& p) { int gx, gz; m_Grid.WorldToCell(p, gx, gz); return m_Grid.IsWalkable(gx, gz); };
    auto place = [&](const Vector3& p, const Vector3& dir)
        {
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.2f, p.z);
            rb.velocity = Vector3::Zero;
            st.slideActive = false;
            s_Dir = dir;
            m_Camera.Camera().SetYaw(DirectX::XMConvertToDegrees(std::atan2(-dir.x, dir.z)));   // 進行方向を映す
            m_Camera.Camera().SnapToTarget();
            s_Phase = 0.0f;
            s_Log = 0.0f;
        };
    char line[200];

    // ---- 1) 坂を探して置く ----
    if (m_AutoStep == 0 && m_AutoTime >= 2.0f)
    {
        float best = 0.0f;
        int bestSteps = 0;
        Vector3 start, dir(0, 0, 1);
        for (int gz = 2; gz < D - 2; ++gz)
            for (int gx = 2; gx < W - 2; ++gx)
            {
                if (!m_Grid.IsWalkable(gx, gz)) continue;
                const Vector3 c = m_Grid.CellToWorld(gx, gz);
                for (int k = 0; k < 8; ++k)
                {
                    const float a = k * DirectX::XM_PIDIV4;
                    const Vector3 d(std::sin(a), 0.0f, std::cos(a));
                    float prev = m_Grid.SampleHeight(c.x, c.z), drop = 0.0f;
                    int steps = 0;
                    for (int s = 1; s <= 40; ++s)   // 大きい丘の坂は 20m 前後
                    {
                        const Vector3 p = c + d * (float)s;
                        if (!walkableAt(p)) break;
                        const float h = m_Grid.SampleHeight(p.x, p.z);
                        const float dh = prev - h;
                        if (dh < 0.15f || dh > 0.9f) break;   // 1m で約 9〜42 度の下り
                        drop += dh; prev = h; ++steps;
                    }
                    if (drop > best) { best = drop; bestSteps = steps; start = c; dir = d; }
                }
            }
        place(start, dir);
        s_SlopeStart = start;
        s_SlopeDir = dir;
        snprintf(line, sizeof(line), "slide A: slope start %.1f,%.1f dir %.2f,%.2f drops %.2f m over %d m",
            start.x, start.z, dir.x, dir.z, best, bestSteps);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    // ---- 3) 平地を探して置く ----
    else if (m_AutoStep == 2)
    {
        Vector3 start, dir(0, 0, 1);
        bool found = false;
        for (int r = 3; r < W / 2 && !found; ++r)   // 中央から外へ
            for (int gz = D / 2 - r; gz <= D / 2 + r && !found; ++gz)
                for (int gx = W / 2 - r; gx <= W / 2 + r && !found; ++gx)
                {
                    if (gx < 2 || gz < 2 || gx >= W - 2 || gz >= D - 2 || !m_Grid.IsWalkable(gx, gz)) continue;
                    const Vector3 c = m_Grid.CellToWorld(gx, gz);
                    const float h0 = m_Grid.SampleHeight(c.x, c.z);
                    for (int k = 0; k < 8 && !found; k += 2)
                    {
                        const float a = k * DirectX::XM_PIDIV4;
                        const Vector3 d(std::sin(a), 0.0f, std::cos(a));
                        bool ok = true;
                        for (int s = 1; s <= 12 && ok; ++s)
                        {
                            const Vector3 p = c + d * (float)s;
                            ok = walkableAt(p) && std::fabs(m_Grid.SampleHeight(p.x, p.z) - h0) < 0.02f;
                        }
                        if (ok) { found = true; start = c; dir = d; }
                    }
                }
        place(start, dir);
        snprintf(line, sizeof(line), "slide B: flat start %.1f,%.1f dir %.2f,%.2f found %d", start.x, start.z, dir.x, dir.z, found ? 1 : 0);
        AutoTestLog(line);
        m_AutoStep = 3;
    }
    // ---- 5) 一番高いマス（高台の 2 段目の上など）に立たせ、35m 引いて 3 方向から全景を撮らせる ----
    else if (m_AutoStep == 4)
    {
        float best = -1.0f;
        Vector3 top;
        for (int gz = 2; gz < D - 2; ++gz)
            for (int gx = 2; gx < W - 2; ++gx)
            {
                if (!m_Grid.IsWalkable(gx, gz)) continue;
                const Vector3 c = m_Grid.CellToWorld(gx, gz);
                const float h = m_Grid.SampleHeight(c.x, c.z);
                if (h > best) { best = h; top = c; }
            }
        // 下りの向き：4 方向のうち、歩けて高さが上がらない（1m で 0.9m 以下の下り）まま一番遠くまで行ける向き
        Vector3 down(1.0f, 0.0f, 0.0f);
        int reach = 0;
        for (int k = 0; k < 4; ++k)
        {
            const Vector3 d = (k == 0) ? Vector3(1, 0, 0) : (k == 1) ? Vector3(-1, 0, 0) : (k == 2) ? Vector3(0, 0, 1) : Vector3(0, 0, -1);
            float prev = best;
            int n = 0;
            for (int s = 1; s <= 80; ++s)
            {
                const Vector3 p = top + d * (float)s;
                if (!walkableAt(p)) break;
                const float h = m_Grid.SampleHeight(p.x, p.z);
                if (h > prev + 0.05f || prev - h > 0.9f) break;
                prev = h;
                n = s;
            }
            if (n > reach) { reach = n; down = d; }
        }
        s_SlopeDir = down;   // 以降は全景用に使う
        place(top, down);
        auto& cam = m_Camera.Camera();
        cam.SetPitch(15.0f);
        cam.distance = 45.0f;
        cam.avoidOcclusion = false;
        snprintf(line, sizeof(line), "slide view: highest %.1f m at %.1f,%.1f, walks down %.0f,%.0f for %d m",
            best, top.x, top.z, down.x, down.z, reach);
        AutoTestLog(line);
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5)
    {
        // 下りの向きと直角に、左から横顔（1.5 秒）→ 右から見下ろし（1.5 秒。崖の影が地面に落ちるのを見る）
        s_Phase += dt;
        const float side = (s_Phase < 1.5f) ? 1.0f : -1.0f;
        m_Camera.Camera().SetYaw(DirectX::XMConvertToDegrees(std::atan2(-s_SlopeDir.z * side, -s_SlopeDir.x * side)));
        m_Camera.Camera().SetPitch(side > 0.0f ? 15.0f : 50.0f);
        if (s_Phase >= 3.0f) { AutoTestLog("slide done"); m_AutoStep = 6; }
    }

    if (m_AutoStep != 1 && m_AutoStep != 3)
    {
        pcs.testInput = false;
        return;
    }

    // ---- 入力の代わり: 決めた向きへ走る / 滑る / 跳ぶ ----
    s_Phase += dt;
    const auto& cam = m_Camera.Camera();
    Vector3 camF = cam.GetForward(); camF.y = 0; camF.Normalize();
    Vector3 camR = cam.GetRight();   camR.y = 0; camR.Normalize();
    pcs.testInput = true;
    pcs.testMove = Vector2(s_Dir.Dot(camR), s_Dir.Dot(camF));
    pcs.testJump = false;
    if (m_AutoStep == 1)
    {
        pcs.testSlide = (s_Phase >= 0.4f && s_Phase < 3.4f);
        if (s_Phase >= 3.4f && s_Phase - dt < 3.4f) pcs.testJump = true;
    }
    else
        pcs.testSlide = (s_Phase >= 0.4f && s_Phase < 3.0f);

    s_Log += dt;
    if (s_Log >= 0.1f)
    {
        s_Log = 0.0f;
        const float hs = std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
        const float slope = DirectX::XMConvertToDegrees(std::acos((std::min)(1.0f, rb.groundNormal.y)));
        snprintf(line, sizeof(line), "slide t %.1f speed %.2f vy %.2f slope %.0f grounded %d sliding %d input %d y %.2f | cam speed %.1f fov %.1f +dist %.2f",
            s_Phase, hs, rb.velocity.y, slope, rb.isGrounded ? 1 : 0, st.slideActive ? 1 : 0, pcs.testSlide ? 1 : 0, tf.position.y,
            m_Camera.Camera().GetSpeed(), m_Camera.Camera().GetEffectiveFov(), m_Camera.Camera().GetSpeedExtraDistance());
        AutoTestLog(line);
    }

    const float end = (m_AutoStep == 1) ? 4.6f : 3.8f;
    if (s_Phase >= end)
    {
        if (m_AutoStep == 1) m_AutoStep = 2;
        else m_AutoStep = 4;
    }
}

// ============================================================
// TEMP-TEST: 魔法書の木箱（VFXL_BATTLE_AUTOTEST=chest）
// 1 秒: 湧き停止、背包は火球だけ、魔法書へ形の違う物を 9 個（十字の隕石・縦 2 の矢・L 字の弧…）→ 背包を開く
// 0.5 秒毎に "chest t bodies contacts asleep"。5 秒 "chest look settled"（外から撮る）
// 6 秒: 4 個追加で積み上げ → 10 秒 "chest look pile" → 11 秒 "chest done"
// ============================================================
void CollisionTestScene::UpdateAutoTestChest(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    static float s_NextLog = 0.0f;
    if (m_AutoStep >= 1 && m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        const auto& sb = m_GameUI.GetSpellbook();
        char line[128];
        snprintf(line, sizeof(line), "chest t %.1f bodies %d contacts %d asleep %d maxV %.1f maxW %.3f",
            m_AutoTime, sb.GetBodyCount(), sb.GetContactCount(), sb.IsAsleep() ? 1 : 0,
            sb.GetMaxSpeed(), sb.GetMaxAngSpeed());
        AutoTestLog(line);
    }

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Fireball, lo + 1, lo + 1, 0);
            bp.dirty = true;
        }
        if (m_Registry.Has<SpellbookComponent>(m_Player))
        {
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            for (ItemID id : { ItemID::Meteor, ItemID::GoldenArrow, ItemID::ArcBolt, ItemID::StoneShot,
                ItemID::HomingBolt, ItemID::HasteRune, ItemID::SplitRune, ItemID::Magnifier, ItemID::DoubleCastRune })
                book.Learn(id);
        }
        m_GameUI.TestShow(1);
        AutoTestLog("chest open");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 5.0f)
    {
        AutoTestLog("chest look settled");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 6.0f)
    {
        if (m_Registry.Has<SpellbookComponent>(m_Player))
        {
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            for (ItemID id : { ItemID::Meteor, ItemID::ArcBolt, ItemID::GoldenArrow, ItemID::Fireball })
                book.Learn(id);
        }
        AutoTestLog("chest add 4");
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 10.0f)
    {
        AutoTestLog("chest look pile");
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 11.0f)
    {
        AutoTestLog("chest done");
        m_AutoStep = 5;
    }
}

// ============================================================
// TEMP-TEST: 魔導光線（VFXL_BATTLE_AUTOTEST=beam）
// 1 秒: 湧き停止・全消し・無敵・魔力無限。3x3 枠の中段に光線（横 3）、上段中央に追尾弾、下段中央に弧
//       （どちらの上下左右も光線の中心に掛かる）。正面 12m に動かない的を 3 体。右横から（光線が画面を横切る）
// 0.5 秒毎に 届いた誘発・撃った回数・出ている光線・範囲・雑魚の数を記録。
// 光線が出た瞬間から 0.6 秒後（溜めが終わって光線が伸びた所）に "beam look <n>"（外から撮る）。3 本撮ったら done
// ============================================================
void CollisionTestScene::UpdateAutoTestBeam()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& cam = m_Camera.Camera();
    static float s_NextLog = 0.0f;
    static float s_LookAt = -1.0f;
    static int s_Looks = 0;
    static bool s_WasActive = false;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -1.5f, 0.0f, 1.5f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 12.0f), 100000.0f, 0.0f);
        // 的の後ろ（16 / 20m）に HP 40 を 2 体：弾は手前の的で消えるので届かない。光線が貫通していれば倒れる（kills で確認）
        m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 16.0f), 40.0f, 0.0f);
        m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 20.0f), 40.0f, 0.0f);

        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        const int lo = BackpackComponent::GRID / 2 - 1;
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Beam, lo, lo + 1, 0);            // 上段（横 3 マス）
        BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo + 2, 0);  // 右中（十字の上が光線の右端）
        BackpackLogic::Place(bp, ItemID::ArcBolt, lo + 1, lo, 0);         // 左中（十字の上が光線の左端）
        bp.dirty = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;

        cam.SetYaw(-90.0f);   // 右から
        cam.distance = 16.0f;
        cam.SetPitch(18.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("beam start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1)
    {
        const bool active = m_WeaponSystem.GetActiveBeamCount() > 0;
        if (active && !s_WasActive) s_LookAt = m_AutoTime + 0.6f;
        s_WasActive = active;

        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog = m_AutoTime + 0.5f;
            const auto& c = m_Swarm.GetCounters();
            char line[160];
            snprintf(line, sizeof(line), "beam t %.1f events %u casts %u beams %d proj %u areas %u kills %u",
                m_AutoTime, m_WeaponSystem.GetTriggerEventsSeen(), m_WeaponSystem.GetTriggeredCasts(),
                m_WeaponSystem.GetActiveBeamCount(), c.aliveProjectiles, c.aliveAreas, c.killCount);
            AutoTestLog(line);
        }
        static bool s_Dumped = false;
        if (!s_Dumped && m_AutoTime >= 2.0f)
        {
            // 杖の中身（誘発の bit が立っているか）
            s_Dumped = true;
            const auto& wand = m_Registry.Get<WandComponent>(m_Player);
            std::string t = "beam wand:";
            for (const auto& s : wand.spells)
            {
                char b[96];
                snprintf(b, sizeof(b), " [spell id %d trig %d mask %u]", (int)s.id, s.triggered ? 1 : 0, s.triggerMask);
                t += b;
            }
            for (const auto& a : wand.areas)
            {
                char b[96];
                snprintf(b, sizeof(b), " [area id %d trig %d profile %d r %.2f]", (int)a.id, a.triggered ? 1 : 0, a.profile, a.radius);
                t += b;
            }
            AutoTestLog(t.c_str());
        }
        if (s_LookAt > 0.0f && m_AutoTime >= s_LookAt)
        {
            s_LookAt = -1.0f;
            char line[64];
            snprintf(line, sizeof(line), "beam look %d", s_Looks);
            AutoTestLog(line);
            if (++s_Looks >= 3)
            {
                // 最後に背包を開いて光線の説明（発動中 / 太さ / 射程）を出す。以後 gameplay は止まる
                auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
                int beamIdx = -1;
                for (int i = 0; i < (int)bp.items.size(); ++i)
                    if (bp.items[i].id == ItemID::Beam) beamIdx = i;
                {
                    const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, beamIdx);
                    std::string l = "beam sheet :";
                    for (const auto& tr : sh.traits) { l += " ["; for (wchar_t w : tr) l += (w < 128) ? (char)w : '?'; l += "]"; }
                    AutoTestLog(l.c_str());
                }
                m_GameUI.TestShow(1);
                m_GameUI.TestTooltip(beamIdx, { m_ScreenW * 0.62f, m_ScreenH * 0.30f });
                AutoTestLog("beam look backpack");
                AutoTestLog("beam done");
                m_AutoStep = 2;
            }
        }
        if (m_AutoTime >= 25.0f) { AutoTestLog("beam done (timeout)"); m_AutoStep = 2; }
    }
}

// ============================================================
// TEMP-TEST: 雑魚の壁抜け・壁詰まり（VFXL_BATTLE_AUTOTEST=stuck）
// 1 秒: 湧き停止・全消し・無敵。玩家の周り 6〜24m の「塞がったマス」（木・岩・台地の箱。外周は除く）を
//       最大 8 個選び、その中心に雑魚を 3 体ずつ「わざと壁の中に」出す。普通の位置にも 40 体
// 以後 0.5 秒毎に敵の池を読み戻し、生きている雑魚のうち塞がったマスに居る数を記録（`stuck t alive inWall`）。
// 期待: 数秒で inWall が 0 になり、その後も 0 のまま（壁から出て、二度と入らない）。12 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestStuck()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    static float s_NextLog = 0.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;

        // 塞がったマス（外周の 1 マスは除く）を近い順に
        std::vector<std::pair<float, Vector3>> blocked;
        for (int gz = 1; gz < m_Grid.Depth() - 1; ++gz)
            for (int gx = 1; gx < m_Grid.Width() - 1; ++gx)
            {
                if (m_Grid.IsWalkable(gx, gz)) continue;
                const Vector3 c = m_Grid.CellToWorld(gx, gz);
                const float d = (Vector3(c.x, 0, c.z) - Vector3(pp.x, 0, pp.z)).Length();
                if (d < 6.0f || d > 24.0f) continue;
                blocked.push_back({ d, c });
            }
        std::sort(blocked.begin(), blocked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        int cells = 0;
        for (size_t i = 0; i < blocked.size() && cells < 8; ++i, ++cells)
            for (int k = 0; k < 3; ++k)
                m_Swarm.SpawnEnemy(Vector3(blocked[i].second.x + (k - 1) * 0.3f, gy, blocked[i].second.z), 100000.0f, 3.5f);
        for (int k = 0; k < 40; ++k)
        {
            const float a = (float)k * 0.157f;
            const float r = 8.0f + (float)(k % 5) * 2.0f;
            const Vector3 p(pp.x + std::cos(a) * r, gy, pp.z + std::sin(a) * r);
            int gx, gz;
            m_Grid.WorldToCell(p, gx, gz);
            if (m_Grid.IsWalkable(gx, gz)) m_Swarm.SpawnEnemy(p, 100000.0f, 3.5f);
        }
        char line[96];
        snprintf(line, sizeof(line), "stuck start: %d blocked cells x3 inside walls, 40 normal", cells);
        AutoTestLog(line);
        s_NextLog = m_AutoTime + 0.05f;   // 最初の 1 回は出した直後（壁の中に居ることを確認）
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        int alive = 0, inWall = 0, nan = 0;
        if (m_Swarm.DebugReadEnemies(enemies, states))
        {
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                ++alive;
                const Vector3 p = enemies[i].position;
                if (p.x != p.x || p.z != p.z) { ++nan; continue; }
                int gx, gz;
                m_Grid.WorldToCell(p, gx, gz);
                if (!m_Grid.IsWalkable(gx, gz)) ++inWall;
            }
        }
        char line[96];
        snprintf(line, sizeof(line), "stuck t %.1f alive %d inWall %d nan %d", m_AutoTime, alive, inWall, nan);
        AutoTestLog(line);
        if (m_AutoTime >= 12.0f) { AutoTestLog("stuck done"); m_AutoStep = 2; }
    }
}

// ============================================================
// TEMP-TEST: 障害物の横での食い込み（VFXL_BATTLE_AUTOTEST=clip、2026-10-01）
// 「雑魚が壁に嵌まって見える」の切り分け。stuck は中心が塞がったマスに居るかしか数えないので、
// ここでは体の円（半径 enemyRadius × 体格）が塞がったマス / 崖（隣のマスとの高さの差 > 1.5m）へ
// どれだけ食い込むかを測る。場面 = 木（1 マスの塊）/ 岩（2〜6 マスの塊）/ 外周（北の縁）/
// 崖（地面のマスの隣が坂のある台地 / 高台）/ plateau（隣が坂の無い台地の箱）/ 崖 + 精英 /
// climb（崖の台地の上に立ち、地面の雑魚が坂を登って来るか。high = 高さ 2m 超の数、16 秒）。各場面: 玩家を障害物の隣のマスに立たせ（障害物寄りに 0.4m）、
// 周り 6〜11m に雑魚 30 体（精英の場面は精英 8 体）。施法停止・無敵。
// 0.5 秒毎に `clip <場面> t alive near inWall big n/max small n/max cliff n/max`
// （near = 障害物の中心から 3m 以内、n = 0.1m 超えて食い込んでいる数、max = 一番深い食い込み m）。
// 6 秒 `clip look <場面> side`（横から）、8 秒 `clip look <場面> top`（真上寄り）、外から撮る
// ============================================================
void CollisionTestScene::UpdateAutoTestClip()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;

    struct Scene { const char* name; int ox, oz, px, pz; bool elite; bool ok; };
    static std::vector<Scene> s_Scenes;
    static int s_Index = 0;
    static float s_Start = 0.0f, s_NextLog = 0.0f;
    static int s_Phase = 0;
    static std::vector<int> s_Comp, s_CompSize;   // 塞がったマスの塊の番号 / 大きさ

    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    auto cellH = [&](int gx, int gz) { const Vector3 c = m_Grid.CellToWorld(gx, gz); return m_Grid.SampleHeight(c.x, c.z); };
    auto walk = [&](int gx, int gz) { return gx >= 0 && gz >= 0 && gx < W && gz < D && m_Grid.IsWalkable(gx, gz); };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        cam.avoidOcclusion = false;
        const Vector3 p0 = tf.position;
        int pgx, pgz;
        m_Grid.WorldToCell(p0, pgx, pgz);

        // 塞がったマスの塊（外周の 1 マスは除く、4 近傍）
        std::vector<int> comp((size_t)W * D, -1);
        std::vector<int> compSize;
        for (int z = 1; z < D - 1; ++z)
            for (int x = 1; x < W - 1; ++x)
            {
                if (m_Grid.IsWalkable(x, z) || comp[(size_t)z * W + x] >= 0) continue;
                const int id = (int)compSize.size();
                int n = 0;
                std::vector<std::pair<int, int>> st{ { x, z } };
                comp[(size_t)z * W + x] = id;
                while (!st.empty())
                {
                    auto [cx, cz] = st.back(); st.pop_back(); ++n;
                    const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (auto& d : nb)
                    {
                        const int nx = cx + d[0], nz = cz + d[1];
                        if (nx < 1 || nz < 1 || nx >= W - 1 || nz >= D - 1) continue;
                        if (m_Grid.IsWalkable(nx, nz) || comp[(size_t)nz * W + nx] >= 0) continue;
                        comp[(size_t)nz * W + nx] = id;
                        st.push_back({ nx, nz });
                    }
                }
                compSize.push_back(n);
            }

        // 障害物の隣で、玩家が立てる平らなマス（周り 3x3 が歩けて高さが同じ）
        auto standCell = [&](int ox, int oz, int& sx, int& sz) -> bool
            {
                const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (auto& d : nb)
                {
                    const int x = ox + d[0], z = oz + d[1];
                    if (!walk(x, z)) continue;
                    const float h = cellH(x, z);
                    bool flat = true;
                    for (int dz = -1; dz <= 1 && flat; ++dz)
                        for (int dx = -1; dx <= 1 && flat; ++dx)
                        {
                            const int ax = x + dx, az = z + dz;
                            if (ax == ox && az == oz) continue;
                            if (walk(ax, az) && std::fabs(cellH(ax, az) - h) > 0.3f) flat = false;
                        }
                    if (flat) { sx = x; sz = z; return true; }
                }
                return false;
            };
        auto nearestBlob = [&](int minSize, int maxSize, Scene& sc)
            {
                float best = 1e30f;
                for (int z = 2; z < D - 2; ++z)
                    for (int x = 2; x < W - 2; ++x)
                    {
                        const int id = comp[(size_t)z * W + x];
                        if (id < 0 || compSize[id] < minSize || compSize[id] > maxSize) continue;
                        const float d2 = (float)((x - pgx) * (x - pgx) + (z - pgz) * (z - pgz));
                        if (d2 < 4.0f || d2 >= best) continue;
                        int sx, sz;
                        if (!standCell(x, z, sx, sz)) continue;
                        best = d2; sc.ox = x; sc.oz = z; sc.px = sx; sc.pz = sz; sc.ok = true;
                    }
            };

        Scene tree{ "tree", 0, 0, 0, 0, false, false };
        nearestBlob(1, 1, tree);
        Scene rock{ "rock", 0, 0, 0, 0, false, false };
        nearestBlob(2, 6, rock);

        Scene wall{ "wall", 0, 0, 0, 0, false, false };
        for (int k = 0; k < W / 2 && !wall.ok; ++k)
            for (int sgn = -1; sgn <= 1 && !wall.ok; sgn += 2)
            {
                const int x = W / 2 + sgn * k;
                if (walk(x, D - 2) && walk(x - 1, D - 2) && walk(x + 1, D - 2) && walk(x, D - 3) && cellH(x, D - 2) < 0.3f)
                { wall.ox = x; wall.oz = D - 1; wall.px = x; wall.pz = D - 2; wall.ok = true; }
            }

        // 崖：地面のマス（高さ < 0.3）で、4 近傍の 1 つが 2m 以上高い歩けるマス（坂のある台地・高台）。
        // plateau：同じく、4 近傍の 1 つが大きな塞がった塊（坂の無い台地の箱）
        auto findCliff = [&](bool blockedBlob, Scene& sc)
            {
                float best = 1e30f;
                for (int z = 3; z < D - 3; ++z)
                    for (int x = 3; x < W - 3; ++x)
                    {
                        if (!walk(x, z) || cellH(x, z) > 0.3f) continue;
                        const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                        for (auto& d : nb)
                        {
                            const int nx = x + d[0], nz = z + d[1];
                            const int id = comp[(size_t)nz * W + nx];
                            const bool high = blockedBlob ? (id >= 0 && compSize[id] > 6)
                                                          : (walk(nx, nz) && cellH(nx, nz) > 2.0f);
                            if (!high) continue;
                            // 反対側は平らな地面（群れが来られる）
                            if (!walk(x - d[0], z - d[1]) || cellH(x - d[0], z - d[1]) > 0.3f) continue;
                            const float d2 = (float)((x - pgx) * (x - pgx) + (z - pgz) * (z - pgz));
                            if (d2 < best) { best = d2; sc.ox = nx; sc.oz = nz; sc.px = x; sc.pz = z; sc.ok = true; }
                        }
                    }
            };
        Scene cliff{ "cliff", 0, 0, 0, 0, false, false };
        findCliff(false, cliff);
        Scene plateau{ "plateau", 0, 0, 0, 0, false, false };
        findCliff(true, plateau);
        Scene elite = cliff;
        elite.name = "elite";
        elite.elite = true;

        // climb：崖の場面の台地の上に立つ（坂を登って来られるか。体の判定で坂に入れなくなっていないか）
        Scene climb{ "climb", cliff.px, cliff.pz, cliff.ox, cliff.oz, false, cliff.ok };

        s_Comp = comp;
        s_CompSize = compSize;
        s_Scenes = { tree, rock, wall, cliff, plateau, elite, climb };
        for (auto& sc : s_Scenes)
        {
            char line[160];
            const Vector3 o = m_Grid.CellToWorld(sc.ox, sc.oz);
            snprintf(line, sizeof(line), "clip scene %s ok %d obstacle (%d,%d) world (%.1f,%.1f) h %.2f stand (%d,%d) h %.2f",
                sc.name, sc.ok ? 1 : 0, sc.ox, sc.oz, o.x, o.z, sc.ok ? cellH(sc.ox, sc.oz) : 0.0f, sc.px, sc.pz,
                sc.ok ? cellH(sc.px, sc.pz) : 0.0f);
            AutoTestLog(line);
        }
        s_Index = -1;
        s_Phase = 9;   // 次の場面へ
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    // ---- 次の場面を始める ----
    if (s_Phase == 9)
    {
        ++s_Index;
        while (s_Index < (int)s_Scenes.size() && !s_Scenes[s_Index].ok) ++s_Index;
        if (s_Index >= (int)s_Scenes.size())
        {
            AutoTestLog("clip done");
            m_AutoStep = 2;
            return;
        }
        const Scene& sc = s_Scenes[s_Index];
        m_Swarm.KillAll();
        const Vector3 o = m_Grid.CellToWorld(sc.ox, sc.oz);
        const Vector3 c = m_Grid.CellToWorld(sc.px, sc.pz);
        Vector3 d(o.x - c.x, 0.0f, o.z - c.z);
        d.Normalize();
        const float px = c.x + d.x * 0.4f, pz = c.z + d.z * 0.4f;
        tf.position = Vector3(px, m_Grid.SampleHeight(px, pz) + 1.0f, pz);
        if (m_Registry.Has<RigidbodyComponent>(m_Player))
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;

        const float gy = m_Swarm.GetAIParams().groundY;
        const int count = sc.elite ? 8 : 30;
        const bool climbScene = (strcmp(sc.name, "climb") == 0);
        int spawned = 0;
        for (int k = 0; k < count * 4 && spawned < count; ++k)
        {
            const float a = (float)k * 2.399f;
            const float r = 6.0f + (float)(k % 6);
            const Vector3 p(px + std::cos(a) * r, gy, pz + std::sin(a) * r);
            int gx, gz;
            m_Grid.WorldToCell(p, gx, gz);
            if (!walk(gx, gz)) continue;
            if (climbScene && cellH(gx, gz) > 0.3f) continue;   // climb は地面にだけ出す（坂を登らせる）
            m_Swarm.SpawnEnemy(p, 100000.0f, 3.5f, sc.elite ? Swarm::kEnemyKindElite : Swarm::kEnemyKindMob);
            ++spawned;
        }

        // 横から：前方 = 玩家 → 障害物 を 90 度回した向き（障害物と玩家が左右に並ぶ）
        // FollowCamera: forward = (-sin(yaw), ., cos(yaw))
        const float fx = -d.z, fz = d.x;
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-fx, fz)));
        cam.SetPitch(25.0f);
        cam.distance = sc.elite ? 13.0f : 9.0f;
        cam.SnapToTarget();

        char line[96];
        snprintf(line, sizeof(line), "clip begin %s spawned %d", sc.name, spawned);
        AutoTestLog(line);
        s_Start = m_AutoTime;
        s_NextLog = m_AutoTime + 0.5f;
        s_Phase = 0;
        return;
    }

    const Scene& sc = s_Scenes[s_Index];
    const float t = m_AutoTime - s_Start;

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        const float r = m_Swarm.GetAIParams().enemyRadius * (sc.elite ? m_Swarm.GetBomberParams().eliteScale : 1.0f);
        const Vector3 o = m_Grid.CellToWorld(sc.ox, sc.oz);
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        // big = 大きな塊（坂の無い台地の箱）と外周のマス：見た目の面がマスの縁と一致するので、マスへの食い込み = 見た目の食い込み。
        // small = 木・岩（1〜6 マス）：見た目は塞いだマスより小さい（木は幹がマスの中央）ので参考値。
        // cliff = 体の円周 16 方向へ 0.05m 刻みで高さ場を引き、足元より 0.5m 以上高い所（崖の面）が体の内側に入る深さ
        int alive = 0, nearCnt = 0, inWall = 0, bigPen = 0, smallPen = 0, cliffPen = 0, high = 0;
        char worst[160] = "";   // 崖に一番深く食い込んでいる 1 体（原因を追う用）
        float maxBig = 0.0f, maxSmall = 0.0f, maxCliff = 0.0f;
        if (m_Swarm.DebugReadEnemies(enemies, states))
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                ++alive;
                const Vector3 p = enemies[i].position;
                if ((Vector3(p.x, 0, p.z) - Vector3(o.x, 0, o.z)).Length() < 3.0f) ++nearCnt;
                if (m_Grid.SampleHeight(p.x, p.z) > 2.0f) ++high;
                int gx, gz;
                m_Grid.WorldToCell(p, gx, gz);
                if (!walk(gx, gz)) { ++inWall; continue; }
                float bp = 0.0f, sp = 0.0f, cp = 0.0f;
                const int reach = (int)std::ceil(r / GridWorld::kCellSize);
                for (int dz = -reach; dz <= reach; ++dz)
                    for (int dx = -reach; dx <= reach; ++dx)
                    {
                        if (dx == 0 && dz == 0) continue;
                        const int cx = gx + dx, cz = gz + dz;
                        if (cx < 0 || cz < 0 || cx >= W || cz >= D) continue;
                        if (m_Grid.IsWalkable(cx, cz)) continue;
                        const int id = s_Comp[(size_t)cz * W + cx];
                        const bool big = (id < 0) || s_CompSize[id] > 6;   // id < 0 = 外周
                        const Vector3 cc = m_Grid.CellToWorld(cx, cz);
                        const float hs = GridWorld::kCellSize * 0.5f;
                        const float qx = (std::max)(std::fabs(p.x - cc.x) - hs, 0.0f);
                        const float qz = (std::max)(std::fabs(p.z - cc.z) - hs, 0.0f);
                        const float depth = r - std::sqrt(qx * qx + qz * qz);
                        if (big) bp = (std::max)(bp, depth);
                        else     sp = (std::max)(sp, depth);
                    }
                // 高さは 0.5m の高さマスの生の値（双線形だと崖の 0.25m 手前から上がり始めるので、面に触れているだけでも食い込みに見える）
                auto rawH = [&](float x, float z)
                    {
                        const float s = GridWorld::kCellSize / (float)GridWorld::kHeightSub;
                        return m_Grid.HeightAt((int)std::floor((x - m_Grid.OriginX()) / s), (int)std::floor((z - m_Grid.OriginZ()) / s));
                    };
                // 基準は足の高さ（台地の縁から踏み出して縁の高さで支えられている途中 / 落ちている途中は、
                // 中心の下の地面より上に居る。地面基準だと背後の台地を食い込みと誤って数えた）
                const float h = (std::max)(rawH(p.x, p.z), p.y - m_Swarm.GetAIParams().groundY);
                for (int k = 0; k < 16; ++k)
                {
                    const float a = (float)k * DirectX::XM_2PI / 16.0f;
                    const float cx = std::cos(a), sz = std::sin(a);
                    for (float s = 0.05f; s <= r + 1e-4f; s += 0.05f)
                        if (rawH(p.x + cx * s, p.z + sz * s) - h > 0.8f)
                        {
                            if (r - s > maxCliff && r - s > cp)
                                snprintf(worst, sizeof(worst), " worst (%.2f,%.2f) h %.2f dir %d s %.2f h2 %.2f v (%.2f,%.2f) anim %u",
                                    p.x, p.z, h, k, s, rawH(p.x + cx * s, p.z + sz * s),
                                    enemies[i].velocity.x, enemies[i].velocity.z, enemies[i].animIndex);
                            cp = (std::max)(cp, r - s);
                            break;
                        }
                }
                if (bp > 0.1f) ++bigPen;
                if (sp > 0.1f) ++smallPen;
                if (cp > 0.1f) ++cliffPen;
                maxBig = (std::max)(maxBig, bp);
                maxSmall = (std::max)(maxSmall, sp);
                maxCliff = (std::max)(maxCliff, cp);
            }
        char line[400];
        snprintf(line, sizeof(line),
            "clip %s t %.1f alive %d near %d inWall %d big %d/%.2f small %d/%.2f cliff %d/%.2f high %d r %.2f%s",
            sc.name, t, alive, nearCnt, inWall, bigPen, maxBig, smallPen, maxSmall, cliffPen, maxCliff, high, r,
            maxCliff > 0.1f ? worst : "");
        AutoTestLog(line);
    }

    char line[64];
    const float t0 = (strcmp(sc.name, "climb") == 0) ? t - 10.0f : t;   // climb は坂を回って来るので 10 秒長く待つ
    if (s_Phase == 0 && t0 >= 6.0f) { snprintf(line, sizeof(line), "clip look %s side", sc.name); AutoTestLog(line); s_Phase = 1; }
    else if (s_Phase == 1 && t0 >= 6.6f) { cam.SetPitch(65.0f); cam.distance = sc.elite ? 16.0f : 11.0f; s_Phase = 2; }
    else if (s_Phase == 2 && t0 >= 8.0f) { snprintf(line, sizeof(line), "clip look %s top", sc.name); AutoTestLog(line); s_Phase = 3; }
    else if (s_Phase == 3 && t0 >= 8.6f) s_Phase = 9;
}

// ============================================================
// TEMP-TEST: 被弾のノックバック（VFXL_BATTLE_AUTOTEST=knock、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵・施法停止、入力は 0（testInput）。玩家の前 3m に雑魚 1 体（殴られる）。
// 6 秒: 全消しして前 2m に自爆兵 1 体（触れて点火 → 爆発）。
// ノックバックが入る度（knockTime が増えた時）に位置を覚え、knockDuration + 0.15 秒後に
// `knock <melee|blast> dist <水平に動いた m> lift <最高点 - 開始の高さ> dir (x,z)` を記録。12 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestKnock()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;

    static Vector3 s_Start;
    static float s_Watch = -1.0f, s_PeakY = 0.0f, s_LastKnock = 0.0f;
    static bool s_Blast = false;
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
    const auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
    const float gy = m_Swarm.GetAIParams().groundY;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Swarm.SpawnEnemy(Vector3(tf.position.x, gy, tf.position.z + 3.0f), 100000.0f, 3.5f);
        char line[128];
        snprintf(line, sizeof(line), "knock start body %.2f m  melee %.2f bodies  blast %.2f bodies lift %.1f",
            stats.radius * 2.0f, stats.knockMeleeBodies, stats.knockBlastBodies, stats.knockBlastLift);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 6.0f)
    {
        m_Swarm.KillAll();
        m_Swarm.SpawnEnemy(Vector3(tf.position.x, gy, tf.position.z + 2.0f), 100000.0f, 3.5f, Swarm::kEnemyKindBomber);
        AutoTestLog("knock bomber");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 12.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("knock done");
        m_AutoStep = 3;
    }

    // ノックバックが入った（残り時間が増えた）→ 測り始める
    if (st.knockTime > s_LastKnock + 1e-4f && st.knockTime >= st.knockDuration - 1e-4f)
    {
        s_Start = tf.position;
        s_PeakY = tf.position.y;
        s_Blast = std::fabs(st.knockDuration - stats.knockBlastTime) < 1e-4f;
        s_Watch = m_AutoTime + st.knockDuration + 0.15f;
    }
    s_LastKnock = st.knockTime;
    if (s_Watch > 0.0f)
    {
        s_PeakY = (std::max)(s_PeakY, tf.position.y);
        if (m_AutoTime >= s_Watch)
        {
            const Vector3 d = tf.position - s_Start;
            char line[160];
            snprintf(line, sizeof(line), "knock %s dist %.3f lift %.3f dir (%.2f,%.2f) hp %.0f",
                s_Blast ? "blast" : "melee", std::sqrt(d.x * d.x + d.z * d.z), s_PeakY - s_Start.y,
                st.knockX, st.knockZ, m_Registry.Get<HealthComponent>(m_Player).current);
            AutoTestLog(line);
            s_Watch = -1.0f;
        }
    }
}

// ============================================================
// TEMP-TEST: 魔力解放（VFXL_BATTLE_AUTOTEST=surge、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵、正面 10m に動かない的 3 体（HP 10 万）。MP 30 / 100、回復 0（減る一方にする）。
//   開局の追尾弾が自動で撃って MP を使い切る。
// 3 秒: Q（testSurge）→ 3 秒間 MP が減らずに撃ち続けるはず。5 秒: もう一度 Q → 再使用待ちなので何も起きないはず。
// 0.25 秒毎に `surge t mp surge cooldown proj`（proj = 飛んでいる弾の数。MP が尽きると 0 に落ち、
// 解放中は MP が減らないまま撃ち続けて戻るはず）。
// 4 秒 `surge look on`（金の MP バー）、8 秒 `surge look cooldown`、10 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestSurge()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (!m_Registry.Has<ManaComponent>(m_Player)) return;
    auto& mp = m_Registry.Get<ManaComponent>(m_Player);
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;
    static float s_NextLog = 0.0f;
    static int s_Phase = 0;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -1.5f, 0.0f, 1.5f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 10.0f), 100000.0f, 0.0f);
        mp.max = 100.0f;
        mp.current = 30.0f;
        mp.regen = 0.0f;
        mp.surgeTime = mp.surgeCooldownLeft = 0.0f;
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        AutoTestLog("surge start: mp 30/100 regen 0, Q at 3s, Q again at 5s");
        s_NextLog = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    if (s_Phase == 0 && m_AutoTime >= 3.0f) { m_PlayerControlSystem.testSurge = true; AutoTestLog("surge press 1"); s_Phase = 1; }
    else if (s_Phase == 1 && m_AutoTime >= 3.15f) { AutoTestLog("surge look burst"); s_Phase = 10; }   // 始まりの金の爆発
    else if (s_Phase == 10 && m_AutoTime >= 4.0f) { AutoTestLog("surge look on"); s_Phase = 2; }      // 体の光 + 画面の金の縁
    else if (s_Phase == 2 && m_AutoTime >= 5.0f) { m_PlayerControlSystem.testSurge = true; AutoTestLog("surge press 2 (cooldown)"); s_Phase = 3; }
    else if (s_Phase == 3 && m_AutoTime >= 8.0f) { AutoTestLog("surge look cooldown"); s_Phase = 4; }
    else if (s_Phase == 4 && m_AutoTime >= 10.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("surge done");
        m_AutoStep = 2;
        return;
    }

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.25f;
        char line[160];
        snprintf(line, sizeof(line), "surge t %.2f mp %.1f surge %.2f cooldown %.1f proj %u",
            m_AutoTime, mp.current, mp.surgeTime, mp.surgeCooldownLeft, m_Swarm.GetCounters().aliveProjectiles);
        AutoTestLog(line);
    }
}

// ============================================================
// TEMP-TEST: 光線の標的の乗り換え（VFXL_BATTLE_AUTOTEST=beamtrack、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵・魔力無限、背包は beam と同じ（光線 + 追尾弾 + 弧）。
//   A = 正面 10m・HP 60（最初の標的。光線で倒れる）、B = 右 50 度 11m・HP 10 万、C = 左 75 度 11m・HP 10 万。
//   光線が出たら施法を止める（1 本だけ見る）。期待: A を捕まえる → A が死ぬ → 角度の近い B へ
//   beamTurnRate（90 度/秒）以下の速さで回る（C ではない）。
// 光線が出ている間 0.05 秒毎に `beamtrack t yaw hasTarget target(x,z) alive`（yaw = 玩家から見た光線の向き、度、+ = 右）。
// 出てから 0.7 / 1.2 / 1.7 秒に `beamtrack look <n>`（真上寄りから撮る）。光線が消えて 0.5 秒で done
// ============================================================
void CollisionTestScene::UpdateAutoTestBeamTrack()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& cam = m_Camera.Camera();
    static float s_NextLog = 0.0f, s_BeamStart = -1.0f, s_BeamEnd = -1.0f;
    static int s_Looks = 0;
    static Vector3 s_Origin;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        s_Origin = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        auto at = [&](float deg, float dist)
            {
                const float r = DirectX::XMConvertToRadians(deg);
                return Vector3(s_Origin.x + std::sin(r) * dist, gy, s_Origin.z + std::cos(r) * dist);
            };
        m_Swarm.SpawnEnemy(at(0.0f, 10.0f), 60.0f, 0.0f);       // A
        m_Swarm.SpawnEnemy(at(50.0f, 11.0f), 100000.0f, 0.0f);  // B
        m_Swarm.SpawnEnemy(at(-75.0f, 11.0f), 100000.0f, 0.0f); // C

        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        const int lo = BackpackComponent::GRID / 2 - 1;
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Beam, lo, lo + 1, 0);
        BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo + 2, 0);
        BackpackLogic::Place(bp, ItemID::ArcBolt, lo + 1, lo, 0);
        bp.dirty = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;

        cam.SetYaw(0.0f);   // 後ろから +Z を見下ろす
        cam.distance = 18.0f;
        cam.SetPitch(60.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("beamtrack start: A 0deg 10m hp60, B +50deg 11m, C -75deg 11m, turn rate 90");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1)
    {
        Vector3 dir, tgt;
        bool hasT = false;
        const bool active = m_WeaponSystem.GetBeamDebug(0, dir, hasT, tgt);
        if (active && s_BeamStart < 0.0f)
        {
            s_BeamStart = m_AutoTime;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // この 1 本だけ見る
        }
        if (!active && s_BeamStart > 0.0f && s_BeamEnd < 0.0f) s_BeamEnd = m_AutoTime;

        if (active && m_AutoTime >= s_NextLog)
        {
            s_NextLog = m_AutoTime + 0.05f;
            const float yaw = DirectX::XMConvertToDegrees(std::atan2(dir.x, dir.z));
            char line[160];
            snprintf(line, sizeof(line), "beamtrack t %.2f yaw %.1f hasTarget %d target (%.1f,%.1f) alive %u",
                m_AutoTime - s_BeamStart, yaw, hasT ? 1 : 0, tgt.x - s_Origin.x, tgt.z - s_Origin.z,
                m_Swarm.GetCounters().aliveEnemies);
            AutoTestLog(line);
        }
        if (s_BeamStart > 0.0f)
        {
            const float t = m_AutoTime - s_BeamStart;
            if ((s_Looks == 0 && t >= 0.7f) || (s_Looks == 1 && t >= 1.2f) || (s_Looks == 2 && t >= 1.7f))
            {
                char line[32];
                snprintf(line, sizeof(line), "beamtrack look %d", s_Looks++);
                AutoTestLog(line);
            }
        }
        if ((s_BeamEnd > 0.0f && m_AutoTime >= s_BeamEnd + 0.5f) || m_AutoTime >= 20.0f)
        {
            AutoTestLog("beamtrack done");
            m_AutoStep = 2;
        }
    }
}

// ============================================================
// TEMP-TEST: 台地からの飛び降り（VFXL_BATTLE_AUTOTEST=drop、2026-10-01）
// 崖（地面のマスの隣が 2m 以上高い歩けるマス）を玩家の近くで探し、その台地（高さ 2m 超で繋がった歩けるマス）を塗る。
// A（1 秒）: 玩家を崖下の地面に立たせ、台地の上の 3〜12m に雑魚 20 体。飛び降りて来るはず。
// B（11 秒）: 玩家を台地の上（崖から 2 マス内側）へ、雑魚 20 体も台地の上。玩家が同じ高さなので誰も落ちないはず。
// 0.5 秒毎に `drop <A|B> t alive high falling near` （high = 足元 2m 超、falling = 足元より 0.3m 以上浮いている、
// near = 玩家から 3m 以内）。A の 2.5 / 4 秒、B の 4 秒に `drop look <A|B> <n>`。21 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestDrop()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;

    static int s_Gx = -1, s_Gz = -1, s_Px = -1, s_Pz = -1, s_Looks = 0;   // 崖下の地面 / 台地側のマス
    static std::vector<std::pair<int, int>> s_Top;                       // 台地の上のマス
    static float s_Start = 0.0f, s_NextLog = 0.0f, s_NextClip = 0.0f, s_ClipMax = 0.0f;
    static int s_ClipHits = 0;
    static char s_Phase = 'A';
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    auto cellH = [&](int gx, int gz) { const Vector3 c = m_Grid.CellToWorld(gx, gz); return m_Grid.SampleHeight(c.x, c.z); };
    auto walk = [&](int gx, int gz) { return gx >= 0 && gz >= 0 && gx < W && gz < D && m_Grid.IsWalkable(gx, gz); };
    const float gy = m_Swarm.GetAIParams().groundY;

    auto place = [&](int gx, int gz)
        {
            const Vector3 c = m_Grid.CellToWorld(gx, gz);
            tf.position = Vector3(c.x, m_Grid.SampleHeight(c.x, c.z) + 1.0f, c.z);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        };
    auto spawnOnTop = [&](int count)
        {
            const Vector3 pp = tf.position;
            int n = 0;
            for (size_t k = 0; k < s_Top.size() * 4 && n < count; ++k)
            {
                const auto& c = s_Top[(k * 7919) % s_Top.size()];
                const Vector3 w = m_Grid.CellToWorld(c.first, c.second);
                const float d = (Vector3(w.x, 0, w.z) - Vector3(pp.x, 0, pp.z)).Length();
                if (d < 3.0f || d > 12.0f) continue;
                const float j = (float)(k % 5) * 0.3f - 0.6f;
                m_Swarm.SpawnEnemy(Vector3(w.x + j, gy, w.z - j), 100000.0f, 3.5f);
                ++n;
            }
            return n;
        };
    // 横から：前方 = 崖の線に沿う向き（地面 → 台地 を 90 度回す）
    auto sideCamera = [&](float pitch, float dist)
        {
            const Vector3 g = m_Grid.CellToWorld(s_Gx, s_Gz), t = m_Grid.CellToWorld(s_Px, s_Pz);
            Vector3 d(t.x - g.x, 0.0f, t.z - g.z);
            d.Normalize();
            const float fx = -d.z, fz = d.x;
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-fx, fz)));
            cam.SetPitch(pitch);
            cam.distance = dist;
            cam.avoidOcclusion = false;
            cam.SnapToTarget();
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        int pgx, pgz;
        m_Grid.WorldToCell(tf.position, pgx, pgz);
        float best = 1e30f;
        for (int z = 3; z < D - 3; ++z)
            for (int x = 3; x < W - 3; ++x)
            {
                if (!walk(x, z) || cellH(x, z) > 0.3f) continue;
                const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (auto& dd : nb)
                {
                    const int nx = x + dd[0], nz = z + dd[1];
                    if (!walk(nx, nz) || cellH(nx, nz) < 2.0f) continue;
                    if (!walk(x - dd[0], z - dd[1]) || cellH(x - dd[0], z - dd[1]) > 0.3f) continue;
                    const float d2 = (float)((x - pgx) * (x - pgx) + (z - pgz) * (z - pgz));
                    if (d2 < best) { best = d2; s_Gx = x; s_Gz = z; s_Px = nx; s_Pz = nz; }
                }
            }
        if (s_Gx < 0) { AutoTestLog("drop no cliff found"); m_AutoStep = 9; return; }
        // 台地を塗る（高さ 2m 超で 4 近傍に繋がった歩けるマス）
        s_Top.clear();
        std::vector<uint8_t> seen((size_t)W * D, 0);
        std::vector<std::pair<int, int>> st{ { s_Px, s_Pz } };
        seen[(size_t)s_Pz * W + s_Px] = 1;
        while (!st.empty())
        {
            auto c = st.back(); st.pop_back();
            s_Top.push_back(c);
            const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (auto& dd : nb)
            {
                const int nx = c.first + dd[0], nz = c.second + dd[1];
                if (!walk(nx, nz) || seen[(size_t)nz * W + nx] || cellH(nx, nz) < 2.0f) continue;
                seen[(size_t)nz * W + nx] = 1;
                st.push_back({ nx, nz });
            }
        }
        place(s_Gx, s_Gz);
        const int n = spawnOnTop(20);
        sideCamera(25.0f, 14.0f);
        char line[160];
        snprintf(line, sizeof(line), "drop A ground (%d,%d) top (%d,%d) h %.2f plateau cells %d spawned %d",
            s_Gx, s_Gz, s_Px, s_Pz, cellH(s_Px, s_Pz), (int)s_Top.size(), n);
        AutoTestLog(line);
        s_Phase = 'A';
        s_Start = m_AutoTime;
        s_NextLog = m_AutoTime + 0.5f;
        s_Looks = 0;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime - s_Start >= 10.0f)
    {
        m_Swarm.KillAll();
        // 台地の上、崖から 2 マス内側（崖の向きの反対へ）
        const int dx = s_Px - s_Gx, dz = s_Pz - s_Gz;
        int tx = s_Px + dx * 2, tz = s_Pz + dz * 2;
        if (!walk(tx, tz) || cellH(tx, tz) < 2.0f) { tx = s_Px; tz = s_Pz; }
        place(tx, tz);
        const int n = spawnOnTop(20);
        sideCamera(35.0f, 16.0f);
        char line[96];
        snprintf(line, sizeof(line), "drop B player on top (%d,%d) spawned %d", tx, tz, n);
        AutoTestLog(line);
        s_Phase = 'B';
        s_Start = m_AutoTime;
        s_NextLog = m_AutoTime + 0.5f;
        s_Looks = 0;
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime - s_Start >= 10.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("drop done");
        m_AutoStep = 3;
    }

    // 貫通: 0.1 秒毎。体（見た目の幅 = 衝突半径 × 1.15）の中に、足より 0.3m 以上高い地形（崖の面・台地の縁。
    // 坂の段 0.27m は数えない）が入っている深さ。落ちる途中に崖の面が体を切る / 縁で台地に沈む = 画面上の貫通
    if ((m_AutoStep == 1 || m_AutoStep == 2) && m_AutoTime >= s_NextClip)
    {
        s_NextClip = m_AutoTime + 0.1f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        const float hs = GridWorld::kCellSize / (float)GridWorld::kHeightSub;
        const float vr = m_Swarm.GetAIParams().enemyRadius * 1.15f;
        auto rawAt = [&](float x, float z)
            {
                return m_Grid.HeightAt((int)std::floor((x - m_Grid.OriginX()) / hs), (int)std::floor((z - m_Grid.OriginZ()) / hs));
            };
        if (m_Swarm.DebugReadEnemies(enemies, states))
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                const Vector3 p = enemies[i].position;
                const float feet = p.y - gy;
                float pen = 0.0f;
                for (int k = 0; k < 16; ++k)
                {
                    const float a = (float)k * DirectX::XM_2PI / 16.0f;
                    for (float s = 0.05f; s <= vr + 1e-4f; s += 0.05f)
                        if (rawAt(p.x + std::cos(a) * s, p.z + std::sin(a) * s) > feet + 0.3f)
                        {
                            pen = (std::max)(pen, vr - s);
                            break;
                        }
                }
                if (pen > 0.05f) ++s_ClipHits;
                s_ClipMax = (std::max)(s_ClipMax, pen);
            }
    }

    if ((m_AutoStep == 1 || m_AutoStep == 2) && m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        // high = 足元の生の高さマスが 2m 超（本当に台地の上）、hover = 生の地面より 0.5m 以上上（落ちている途中 /
        // 双線形の崖の裾に引っかかっている）
        int alive = 0, high = 0, falling = 0, hover = 0, nearCnt = 0;
        const Vector3 pp = tf.position;
        const float hs = GridWorld::kCellSize / (float)GridWorld::kHeightSub;
        if (m_Swarm.DebugReadEnemies(enemies, states))
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                ++alive;
                const Vector3 p = enemies[i].position;
                const float ground = gy + m_Grid.SampleHeight(p.x, p.z);
                const float raw = m_Grid.HeightAt((int)std::floor((p.x - m_Grid.OriginX()) / hs),
                                                  (int)std::floor((p.z - m_Grid.OriginZ()) / hs));
                if (raw > 2.0f) ++high;
                if (raw > 2.0f && s_Phase == 'A' && m_AutoTime - s_Start > 8.0f)
                {
                    // 降りて来ない 1 体の様子（原因を追う用）
                    char l2[192];
                    snprintf(l2, sizeof(l2), "drop stuck (%.2f,%.2f) y %.2f raw %.2f v (%.2f,%.2f,%.2f) anim %u player (%.2f,%.2f)",
                        p.x, p.z, p.y - gy, raw, enemies[i].velocity.x, enemies[i].velocity.y, enemies[i].velocity.z,
                        enemies[i].animIndex, pp.x, pp.z);
                    AutoTestLog(l2);
                }
                if (p.y > ground + 0.3f) ++falling;
                if (p.y - gy - raw > 0.5f) ++hover;
                if ((Vector3(p.x, 0, p.z) - Vector3(pp.x, 0, pp.z)).Length() < 3.0f) ++nearCnt;
            }
        // clip = この 0.5 秒の間（0.1 秒毎の読み戻し）に体へ崖の面が入っていた延べ数 / 一番深い m
        char line[160];
        snprintf(line, sizeof(line), "drop %c t %.1f alive %d high %d falling %d hover %d near %d clip %d/%.2f",
            s_Phase, m_AutoTime - s_Start, alive, high, falling, hover, nearCnt, s_ClipHits, s_ClipMax);
        AutoTestLog(line);
        s_ClipHits = 0;
        s_ClipMax = 0.0f;
    }
    const float t = m_AutoTime - s_Start;
    if (m_AutoStep == 1 && ((s_Looks == 0 && t >= 1.5f) || (s_Looks == 1 && t >= 2.5f) || (s_Looks == 2 && t >= 4.0f)))
    {
        char line[32];
        snprintf(line, sizeof(line), "drop look A %d", s_Looks++);
        AutoTestLog(line);
    }
    if (m_AutoStep == 2 && s_Looks == 0 && t >= 4.0f)
    {
        AutoTestLog("drop look B 0");
        ++s_Looks;
    }
}

// ============================================================
// TEMP-TEST: 最終波の幽霊（VFXL_BATTLE_AUTOTEST=ghost）
// 1 秒: 湧き停止・全消し・無敵、幽霊を 20 体（玩家の周りの環 25〜35m。壁を素通りするので歩けるマスかは見ない）。
// 0.5 秒毎に敵の池を読み戻し、生きている数・塞がったマスに居る数（幽霊は壁の中を通れる = 0 でなくてよい）・
// 玩家に一番近い幽霊の距離を記録。3 秒 / 5 秒 "ghost look"（外から撮る）、8 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestGhost()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    static float s_NextLog = 0.0f;
    static int s_Looks = 0;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Mobs.QueueDebugGhosts(20);
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 撃たない（数と動きを見る）
        AutoTestLog("ghost start: 20 ghosts");
        s_NextLog = m_AutoTime + 0.5f;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1)
    {
        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog = m_AutoTime + 0.5f;
            std::vector<Swarm::Enemy> enemies;
            std::vector<uint32_t> states;
            int alive = 0, inWall = 0;
            float nearest = 1e9f;
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            if (m_Swarm.DebugReadEnemies(enemies, states))
                for (size_t i = 0; i < enemies.size(); ++i)
                {
                    if (states[i] == Swarm::kStateDead) continue;
                    ++alive;
                    int gx, gz;
                    m_Grid.WorldToCell(enemies[i].position, gx, gz);
                    if (!m_Grid.IsWalkable(gx, gz)) ++inWall;
                    const Vector3 d = enemies[i].position - pp;
                    nearest = (std::min)(nearest, std::sqrt(d.x * d.x + d.z * d.z));
                }
            char line[128];
            snprintf(line, sizeof(line), "ghost t %.1f alive %d inWall %d nearest %.1f", m_AutoTime, alive, inWall, nearest);
            AutoTestLog(line);
        }
        if ((s_Looks == 0 && m_AutoTime >= 3.0f) || (s_Looks == 1 && m_AutoTime >= 5.0f))
        {
            char line[32];
            snprintf(line, sizeof(line), "ghost look %d", s_Looks++);
            AutoTestLog(line);
        }
        if (m_AutoTime >= 8.0f) { AutoTestLog("ghost done"); m_AutoStep = 2; }
    }
}

// ============================================================
// TEMP-TEST: 毒（VFXL_BATTLE_AUTOTEST=poison、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵・MP 無限・入力 0、背包は毒だけ（3x3 の中央）。玩家の +Z 17〜30m に雑魚 16 体
//   （2 列、HP 1000、3.5 m/s）、31m に精鋭 1 体（HP 2000、3.4 m/s で見分ける）。玩家は動かない。
//   射程 15m に入った最寄りの敵の足元へ毒が落ち、後ろから来る敵がその池を通る。
// 0.25 秒毎に DebugReadEnemies（Map で GPU を待つ。自測専用）で
//   `poison t alive free slowed ratio elite hp areas`：free = 玩家から 3m 以上離れた雑魚、
//   slowed = その中で 水平速度 / moveSpeed < 0.8 の数、ratio = slowed の平均（40% 減速なら 0.6 前後）、
//   elite = 精鋭の 速度 / moveSpeed（玩家の近く・死亡は -1。池の中なら 0.8 前後）、hp = 全員の HP 合計（池で減る）
// 鏡頭は玩家の後ろ 16m・俯角 40°。3.5 / 5 / 6.5 秒 `poison look <n>`（外から撮る）、10 秒 done
// ============================================================
void CollisionTestScene::UpdateAutoTestPoison()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;
    static float s_NextLog = 0.0f;
    static int s_Looks = 0;
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const float gy = m_Swarm.GetAIParams().groundY;
        for (int k = 0; k < 16; ++k)
            m_Swarm.SpawnEnemy(Vector3(pp.x + ((k % 2) ? 0.8f : -0.8f), gy, pp.z + 17.0f + (float)(k / 2) * 1.6f),
                1000.0f, 3.5f);
        m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 31.0f), 2000.0f, 3.4f, Swarm::kEnemyKindElite);
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Poison, lo + 1, lo + 1, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.distance = 16.0f;
        cam.SetPitch(40.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("poison start: 16 mobs (3.5 m/s) + 1 elite (3.4 m/s) walking in from +Z 17-31m");
        s_NextLog = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.25f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        if (m_Swarm.DebugReadEnemies(enemies, states))
        {
            int alive = 0, freeN = 0, slowed = 0;
            float ratioSum = 0.0f, elite = -1.0f, hp = 0.0f;
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                const Swarm::Enemy& e = enemies[i];
                ++alive;
                hp += Swarm::HpFromFixed(e.hp);
                const float dx = e.position.x - pp.x, dz = e.position.z - pp.z;
                if (dx * dx + dz * dz < 3.0f * 3.0f) continue;   // 玩家に詰まっている（押し合いで遅い）
                const float speed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
                const float r = speed / (std::max)(e.moveSpeed, 0.01f);
                if (std::fabs(e.moveSpeed - 3.4f) < 1e-3f) { elite = r; continue; }
                ++freeN;
                if (r < 0.8f) { ++slowed; ratioSum += r; }
            }
            char line[200];
            snprintf(line, sizeof(line), "poison t %.2f alive %d free %d slowed %d ratio %.2f elite %.2f hp %.0f areas %u",
                m_AutoTime, alive, freeN, slowed, slowed ? ratioSum / slowed : 0.0f, elite, hp,
                m_Swarm.GetCounters().aliveAreas);
            AutoTestLog(line);
        }
    }
    const float looks[] = { 3.5f, 5.0f, 6.5f };
    if (s_Looks < 3 && m_AutoTime >= looks[s_Looks])
    {
        char line[32];
        snprintf(line, sizeof(line), "poison look %d", s_Looks++);
        AutoTestLog(line);
    }
    if (m_AutoTime >= 10.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("poison done");
        m_AutoStep = 2;
    }
}