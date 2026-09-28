// ============================================================
// FeedbackVFXSystem.cpp
// ============================================================
#include "ECS/System/FeedbackVFXSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/EntryType.h"
#include "Component/TransformComponent.h"
#include "Player/LevelComponent.h"
#include "imgui.h"

using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr const char* kLevelUp = "LevelUp.json";
    constexpr const char* kCrateOpen = "CrateOpen.json";
    constexpr const char* kHurt = "Hurt.json";
    constexpr const char* kExpPickup = "ExpPickup.json";
    constexpr float kLevelUpTime = 1.0f;
    constexpr float kCrateOpenTime = 1.8f;
    constexpr float kHurtTime = 0.5f;
    constexpr float kExpPickupTime = 0.6f;
}

void FeedbackVFXSystem::Init(AreaVFXPlayer* player, const VFXContext* ctx)
{
    m_Player = player;
    m_Ctx = ctx;
    m_PrevLevel = -1;
    m_HurtTimer = 0.0f;
    m_PickupTimer = 0.0f;
}

void FeedbackVFXSystem::Play(const char* file, const Vector3& pos, float duration, bool follow)
{
    if (!m_Player || !m_Ctx) return;
    m_Player->Play(file, pos, duration, follow, *m_Ctx);
    if (onPlayed) onPlayed(file);
}

void FeedbackVFXSystem::Update(Registry& reg, Entity player, float dt, float hpLost)
{
    m_HurtTimer -= dt;
    m_PickupTimer -= dt;
    if (!reg.IsValid(player) || !reg.Has<TransformComponent>(player)) return;
    const Vector3 pos = reg.Get<TransformComponent>(player).position;

    // ---- 升級（レベルは三択を出した時点で上がる）----
    if (reg.Has<LevelComponent>(player))
    {
        const int level = reg.Get<LevelComponent>(player).level;
        if (m_LevelUp && m_PrevLevel >= 0 && level > m_PrevLevel)
            Play(kLevelUp, pos, kLevelUpTime, true);
        m_PrevLevel = level;
    }

    // ---- 被弾（揺れと画面の赤い縁は毎回。斬撃だけ間をあける）----
    if (m_Hurt && hpLost > 0.0f && m_HurtTimer <= 0.0f)
    {
        Play(kHurt, pos, kHurtTime, true);
        m_HurtTimer = m_HurtInterval;
    }
}

void FeedbackVFXSystem::OnCrateOpened(const Vector3& pos)
{
    if (m_Crate) Play(kCrateOpen, pos, kCrateOpenTime, false);
}

void FeedbackVFXSystem::OnExpPicked(const Vector3& pos)
{
    if (!m_ExpPickup || m_PickupTimer > 0.0f) return;
    Play(kExpPickup, pos, kExpPickupTime, true);
    m_PickupTimer = m_PickupInterval;
}

// ============================================================
// ImGui: Feedback VFX 面板
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
    ImGui::TextDisabled("Assets/Data/VFXData/LevelUp / CrateOpen / Hurt / ExpPickup.json");
    ImGui::TextDisabled("hits: ArcBolt -> ArcSpark, HomingBolt -> VoidPop (0 damage areas)");
    if (ImGui::Button("Reload json") && m_Player) m_Player->ClearTemplates();
}
