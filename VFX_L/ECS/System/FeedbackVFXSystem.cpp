// ============================================================
// FeedbackVFXSystem.cpp
// ============================================================
#include "ECS/System/FeedbackVFXSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/EntryType.h"
#include "Component/TransformComponent.h"
#include "Player/LevelComponent.h"
#include "Component/ManaComponent.h"
#include "Player/ShieldComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Audio/AudioSystem.h"
#include "imgui.h"

using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr const char* kLevelUp = "LevelUp.json";
    constexpr const char* kCrateOpen = "CrateOpen.json";
    constexpr const char* kHurt = "Hurt.json";
    constexpr const char* kExpPickup = "ExpPickup.json";
    constexpr const char* kSurgeBurst = "ManaSurgeBurst.json";
    constexpr const char* kSurgeAura = "ManaSurgeAura.json";
    constexpr const char* kShieldHit = "ShieldHit.json";
    constexpr const char* kShieldBreak = "ShieldBreak.json";
    constexpr float kLevelUpTime = 1.0f;
    constexpr float kCrateOpenTime = 1.8f;
    constexpr float kHurtTime = 0.5f;
    constexpr float kExpPickupTime = 0.6f;
    constexpr float kSurgeBurstTime = 0.8f;
    constexpr float kShieldHitTime = 0.35f;
    constexpr float kShieldBreakTime = 0.9f;
}

void FeedbackVFXSystem::Init(AreaVFXPlayer* player, const VFXContext* ctx)
{
    m_Player = player;
    m_Ctx = ctx;
    m_PrevLevel = -1;
    m_HurtTimer = 0.0f;
    m_PickupTimer = 0.0f;
    m_PrevSurgeTime = 0.0f;
    m_SurgeAura = 0;
    m_ShieldRead = false;
}

uint32_t FeedbackVFXSystem::Play(const char* file, const Vector3& pos, float duration, bool follow)
{
    if (!m_Player || !m_Ctx) return 0;
    const uint32_t handle = m_Player->Play(file, pos, duration, follow, *m_Ctx);
    if (onPlayed) onPlayed(file);
    return handle;
}

void FeedbackVFXSystem::Update(Registry& reg, Entity player, float dt, float hpLost)
{
    m_HurtTimer -= dt;
    m_PickupTimer -= dt;
    if (!reg.IsValid(player) || !reg.Has<TransformComponent>(player)) return;
    const Vector3 pos = reg.Get<TransformComponent>(player).position;

    // ---- レベルアップ（レベルは三択を出した時点で上がる）----
    if (reg.Has<LevelComponent>(player))
    {
        const int level = reg.Get<LevelComponent>(player).level;
        if (m_PrevLevel >= 0 && level > m_PrevLevel)
        {
            AudioSystem::Get().Play("level_up");
            if (m_LevelUp) Play(kLevelUp, pos, kLevelUpTime, true);
        }
        m_PrevLevel = level;
    }

    // ---- 被弾（揺れと画面の赤い縁は毎回。斬撃だけ間をあける）----
    if (hpLost > 0.0f) AudioSystem::Get().Play("player_hurt");   // 間隔は cue の minInterval
    if (m_Hurt && hpLost > 0.0f && m_HurtTimer <= 0.0f)
    {
        Play(kHurt, pos, kHurtTime, true);
        m_HurtTimer = m_HurtInterval;
    }

    // ---- 魔力解放（残り時間が増えた = 始まった。光は残り時間だけ出す）----
    if (reg.Has<ManaComponent>(player))
    {
        const float surge = reg.Get<ManaComponent>(player).surgeTime;
        if (m_Surge && surge > m_PrevSurgeTime + 1e-4f)
        {
            AudioSystem::Get().Play("mana_surge");
            if (m_SurgeAura && m_Player) m_Player->StopInstance(m_SurgeAura);
            Play(kSurgeBurst, pos, kSurgeBurstTime, true);
            m_SurgeAura = Play(kSurgeAura, pos, surge, true);
        }
        else if (surge <= 0.0f && m_PrevSurgeTime > 0.0f && m_SurgeAura)
        {
            // 解放が終わった（デバッグパネルで残りを 0 にされた時も）。Test ボタンの光は残り時間で勝手に止まる
            if (m_Player) m_Player->StopInstance(m_SurgeAura);
            m_SurgeAura = 0;
        }
        m_PrevSurgeTime = surge;
    }

    // ---- シールドで受けた（2026-10-07）：六角の護罩が一瞬光る / 割れたら護罩が砕けて飛び散る ----
    // ShieldComponent の累計を前のフレームと比べる（HP は減らないので hpLost では拾えない）
    if (reg.Has<ShieldComponent>(player))
    {
        const auto& sh = reg.Get<ShieldComponent>(player);
        bool hitNow = false;
        if (m_ShieldRead && sh.breaks != m_PrevShieldBreaks)
        {
            AudioSystem::Get().Play("shield_break");
            if (m_Shield) Play(kShieldBreak, pos, kShieldBreakTime, false);   // 破片はその場に残す（付いて動かない）
        }
        else if (m_ShieldRead && sh.hits != m_PrevShieldHits)
        {
            AudioSystem::Get().Play("shield_hit");
            if (m_Shield) Play(kShieldHit, pos, kShieldHitTime, true);   // 赤い火花と光（護罩そのものの赤は m_Bubble）
            hitNow = true;
        }
        m_PrevShieldHits = sh.hits;
        m_PrevShieldBreaks = sh.breaks;
        m_ShieldRead = true;

        // 常に包む護罩（シールドが残っている間。死んだら消す）
        const bool alive = !reg.Has<PlayerStateComponent>(player) || !reg.Get<PlayerStateComponent>(player).IsDead();
        m_Bubble.Update(dt, sh.current, hitNow && m_Shield, alive);
    }
}

void FeedbackVFXSystem::SubmitMeshes(VFXMeshRenderer& renderer, Registry& reg, Entity player)
{
    if (!reg.IsValid(player) || !reg.Has<TransformComponent>(player)) return;
    m_Bubble.Submit(renderer, reg.Get<TransformComponent>(player).position);
}

void FeedbackVFXSystem::OnCrateOpened(const Vector3& pos)
{
    AudioSystem::Get().Play("crate_open");
    if (m_Crate) Play(kCrateOpen, pos, kCrateOpenTime, false);
}

void FeedbackVFXSystem::OnExpPicked(const Vector3& pos)
{
    AudioSystem::Get().Play("exp_pickup");   // 音は cue の間隔で間引く（光より細かく鳴らす）
    if (!m_ExpPickup || m_PickupTimer > 0.0f) return;
    Play(kExpPickup, pos, kExpPickupTime, true);
    m_PickupTimer = m_PickupInterval;
}

// ============================================================
// ImGui: Feedback VFX パネル
// ============================================================
void FeedbackVFXSystem::DrawImGui(Registry& reg, Entity player)
{
    if (!ImGui::CollapsingHeader("Feedback VFX"))
        return;

    const Vector3 here = (reg.IsValid(player) && reg.Has<TransformComponent>(player))
        ? reg.Get<TransformComponent>(player).position : Vector3::Zero;
    ImGui::Checkbox("Level up##fx", &m_LevelUp);
    ImGui::SameLine(160.0f);
    if (ImGui::Button("Test##fxLevel")) Play(kLevelUp, here, kLevelUpTime, true);
    ImGui::Checkbox("Crate open##fx", &m_Crate);
    ImGui::SameLine(160.0f);
    if (ImGui::Button("Test##fxCrate")) Play(kCrateOpen, here, kCrateOpenTime, false);
    ImGui::Checkbox("Hurt##fx", &m_Hurt);
    ImGui::SameLine(160.0f);
    if (ImGui::Button("Test##fxHurt")) Play(kHurt, here, kHurtTime, true);
    ImGui::DragFloat("Hurt min interval (s)", &m_HurtInterval, 0.05f, 0.0f, 5.0f);
    ImGui::Checkbox("Exp pickup##fx", &m_ExpPickup);
    ImGui::SameLine(160.0f);
    if (ImGui::Button("Test##fxExp")) Play(kExpPickup, here, kExpPickupTime, true);
    ImGui::DragFloat("Pickup min interval (s)", &m_PickupInterval, 0.01f, 0.0f, 2.0f);
    ImGui::Checkbox("Mana surge##fx", &m_Surge);
    ImGui::SameLine(160.0f);
    if (ImGui::Button("Test##fxSurge"))
    {
        if (m_SurgeAura && m_Player) m_Player->StopInstance(m_SurgeAura);
        Play(kSurgeBurst, here, kSurgeBurstTime, true);
        m_SurgeAura = Play(kSurgeAura, here, 3.0f, true);
    }
    ImGui::Checkbox("Shield hit / break##fx", &m_Shield);
    ImGui::SameLine(160.0f);
    if (ImGui::Button("Test##fxShieldHit")) Play(kShieldHit, here, kShieldHitTime, true);
    ImGui::SameLine();
    if (ImGui::Button("Test break##fxShieldBreak")) Play(kShieldBreak, here, kShieldBreakTime, false);
    m_Bubble.DrawImGui();
    ImGui::TextDisabled("Assets/Data/VFXData/LevelUp / CrateOpen / Hurt / ExpPickup.json");
    ImGui::TextDisabled("  ManaSurgeBurst / ManaSurgeAura / ShieldHit / ShieldBreak.json");
    ImGui::TextDisabled("hits: ArcBolt -> ArcSpark, HomingBolt -> VoidPop (0 damage areas)");
    if (ImGui::Button("Reload json") && m_Player) m_Player->ClearTemplates();
}
