// ============================================================
// CollisionTestScenePlayerPanels.cpp
// CollisionTestScene: ImGui panels (Wand / Player / Item Database)
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Graphics/Renderer/TerrainSurface.h"

#include "Component/TransformComponent.h"
#include "Component/ClothChainComponent.h"
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
#include "Player/WalletComponent.h"
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
#include <cfloat>
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
    const int lvForRegen = m_Registry.Has<LevelComponent>(m_Player) ? m_Registry.Get<LevelComponent>(m_Player).level : 1;
    const float regen = m_Registry.Has<ManaComponent>(m_Player)
        ? m_Registry.Get<ManaComponent>(m_Player).EffectiveRegen(lvForRegen) : 0.0f;

    ImGui::DragFloat("Range", &w.range, 0.5f, 1.0f, 60.0f);

    // ---- 発射の仕方 ----
    int modeIdx = (int)w.castMode;
    const char* modeNames[] = { "Auto", "Manual", "Debug Burst" };
    if (ImGui::Combo("Cast Mode", &modeIdx, modeNames, 3))
        w.castMode = (CastMode)modeIdx;
    ImGui::Checkbox("Casting Paused (debug)", &w.castingPaused);   // 詠唱停止（2026-10-01 から Q ではなくデバッグ・自動テスト専用）
    // 魔力解放（Q / パッド Y）: 3 秒消費なし、20 秒に 1 回
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
    // 能力アップ「魔法威力」の累積（変えたらバックパックを集約し直す）
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
// ImGui: プレイヤー（ステートマシン + 能力値）
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
        // 回復はレベルで伸びる（2026-10-04）。実際の回復 = Mana Regen × (1 + 下の値 × (Lv - 1))
        ImGui::DragFloat("Mana Regen / Level", &mana.regenPerLevel, 0.005f, 0.0f, 2.0f, "%.3f");
        const int lv = m_Registry.Has<LevelComponent>(m_Player) ? m_Registry.Get<LevelComponent>(m_Player).level : 1;
        ImGui::Text("effective regen %.1f / s (Lv %d)", mana.EffectiveRegen(lv), lv);
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
        // 金貨（2026-10-04）：経験値 × goldPerExp で増える。引き直しの値段 = base + step × 回数
        if (m_Registry.Has<WalletComponent>(m_Player))
        {
            auto& wallet = m_Registry.Get<WalletComponent>(m_Player);
            ImGui::Text("Gold %d   (earned %.0f, spent %.0f)   reroll next %d", wallet.Coins(), wallet.earned, wallet.spent,
                m_LevelUpSystem.RerollCost(lv));
            ImGui::DragFloat("Gold / Exp", &wallet.goldPerExp, 0.005f, 0.0f, 2.0f, "%.3f");
            ImGui::DragInt("Reroll Base Cost", &m_LevelUpSystem.rerollBaseCost, 1, 0, 1000);
            ImGui::DragInt("Reroll Step Cost", &m_LevelUpSystem.rerollStepCost, 1, 0, 1000);
            if (ImGui::Button("+100 Gold")) wallet.Earn(100.0f);
        }
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

    // ---- ステートマシン（3層）----
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

    // ---- アニメ（ステートマシン → クリップの写像の確認用）----
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

        // ---- マントの揺れ（ClothChainSystem、2026-10-04）----
        if (m_Registry.Has<ClothChainComponent>(m_Player) && ImGui::TreeNode("Cape (cloth chain)"))
        {
            ClothChainSystem::DrawImGui(m_Registry.Get<ClothChainComponent>(m_Player));
            ImGui::TreePop();
        }

        // ---- 被弾のノックバック（距離は体分 = 体の幅単位）----
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
            case ItemCategory::Summon:     cat = "Summon";     break;
            case ItemCategory::Unknown:    cat = "Unknown";    break;
            }

            ImGui::TextColored(ImVec4(c->color.x, c->color.y, c->color.z, 1.0f),
                "%-14s [%s]  occupy=%zu  influence=%zu  icon=%s",
                c->name, cat, c->occupyCells.size(), c->influenceCells.size(),
                c->iconPath ? "yes" : "no");
        }
    }
}
