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
#include "Component/Projectile/ProjectileComponent.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerFactory.h"
#include "Player/LevelComponent.h"
#include "ECS/View.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Component/BackpackComponent.h"
#include "Item/ItemTypes.h"
#include "Swarm/AreaProfile.h"
#include "World/TerrainGenerator.h"
#include "VFX_Editor/VFXId.h"
#include "Debug/DebugManager.h"
#include "Core/Application.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>   // TEMP-TEST: autotest.log
#include <random>
#include <unordered_set>

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
    ImGui::Checkbox("Billboard", &m_ShowBillboard);
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
    m_Stress.DrawImGui(m_Swarm, m_CollisionSystem, m_ParticleSystem, m_ProjectileRenderer);
    DrawSwarmPanel();
    DrawItemDatabasePanel();
    DrawTerrainPanel();
    DrawEnemiesPanel();
    if (m_Crates.DrawImGui(m_Interaction, m_LevelUpSystem)) RespawnCrates();
    m_Feedback.DrawImGui(m_Registry, m_Player);
    m_Camera.DrawImGui();
    m_Lighting.DrawImGui(PlayerPos());
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
                    // 投射物は数が多すぎるので描かない
                    if (m_Registry.Has<ProjectileComponent>(e)) return;

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
    ImGui::Checkbox("Casting Paused (Q / Pad Y)", &w.castingPaused);   // プレイヤーの施法停止スイッチ

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
        ImGui::DragFloat("Exp Base", &lv.expBase, 5.0f, 10.0f, 1000.0f);
        ImGui::DragFloat("Exp / Level", &lv.expPerLevel, 5.0f, 0.0f, 500.0f);

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

        ImGui::DragFloat("Move Speed", &stats.moveSpeed, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Jump Power", &stats.jumpPower, 0.1f, 0.0f, 30.0f);
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
        ImGui::DragInt("Grass", &tc.grassCount, 1.0f, 0, 2000);

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
            TerrainGenerator::Generate(m_Registry, device, m_Grid, m_TerrainConfig, m_Terrain);
            m_StaticProps.Build(m_Registry);   // 置物の instanced 表も作り直す

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
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_AutoBomber) { UpdateAutoTestBomber(); return; }
    if (m_AutoPerf) { UpdateAutoTestPerf(); return; }
    if (m_AutoSlide) { UpdateAutoTestSlide(dt); return; }

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
        // 下りの向きと直角に、左右から 1.5 秒ずつ（段々の横顔）
        s_Phase += dt;
        const float side = (s_Phase < 1.5f) ? 1.0f : -1.0f;
        m_Camera.Camera().SetYaw(DirectX::XMConvertToDegrees(std::atan2(-s_SlopeDir.z * side, -s_SlopeDir.x * side)));
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
        snprintf(line, sizeof(line), "slide t %.1f speed %.2f vy %.2f slope %.0f grounded %d sliding %d input %d y %.2f",
            s_Phase, hs, rb.velocity.y, slope, rb.isGrounded ? 1 : 0, st.slideActive ? 1 : 0, pcs.testSlide ? 1 : 0, tf.position.y);
        AutoTestLog(line);
    }

    const float end = (m_AutoStep == 1) ? 4.6f : 3.8f;
    if (s_Phase >= end)
    {
        if (m_AutoStep == 1) m_AutoStep = 2;
        else m_AutoStep = 4;
    }
}
