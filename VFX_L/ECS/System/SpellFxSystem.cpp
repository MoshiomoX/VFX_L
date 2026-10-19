// ============================================================
// SpellFxSystem.cpp
// ============================================================
#include "ECS/System/SpellFxSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/VFXId.h"
#include "Audio/AudioSystem.h"

#include "imgui.h"

using namespace DirectX::SimpleMath;

void SpellFxSystem::Update(float dt, const std::vector<WeaponSystem::TriggeredFx>& fx, const IceSpikeField::HeightFn& heightAt)
{
    for (const auto& f : fx)
    {
        if (f.id == ItemID::IceSpike) CastIceSpike(f.start, f.end, f.scale, heightAt);
    }
    m_IceSpikes.Update(dt);
}

void SpellFxSystem::CastIceSpike(const Vector3& start, const Vector3& end, float scale, const IceSpikeField::HeightFn& heightAt)
{
    ++m_IceSpikeCasts;
    m_IceSpikes.Spawn(start, end, scale, heightAt);

    // 冷気・氷の欠片・光を線に沿って数か所（トゲが届く頃に出るよう、足元から順に。時間差は付けず同時に出す）
    Vector3 dir(end.x - start.x, 0.0f, end.z - start.z);
    const float length = dir.Length();
    if (particles && m_Player && m_Ctx && length > 1e-3f)
    {
        dir /= length;
        const float yaw = DirectX::XMConvertToDegrees(std::atan2(dir.x, dir.z));
        for (float d = (std::min)(2.0f, length); d <= length + 0.01f; d += (std::max)(particleStep, 0.5f))
        {
            Vector3 p = start + dir * d;
            p.y = heightAt ? heightAt(p.x, p.z) : start.y + (end.y - start.y) * d / length;
            const uint32_t h = m_Player->Play(VFXDatabase::GetFile(VFXId::IceSpike), p, 2.5f, false, *m_Ctx);
            if (h) m_Player->RotateInstance(h, yaw);
        }
    }
    AudioSystem::Get().Play("ice_spike");
    AudioSystem::Get().Play("hit_ice");
}

void SpellFxSystem::DrawImGui(const Vector3& testStart, const Vector3& testEnd, const IceSpikeField::HeightFn& heightAt)
{
    ImGui::Text("Ice Spike casts: %u", m_IceSpikeCasts);
    if (ImGui::Button("Test cast (8m in front of the player)")) CastIceSpike(testStart, testEnd, 1.0f, heightAt);
    ImGui::Checkbox("Frost particles (IceSpike.json)", &particles);
    ImGui::DragFloat("Particle step (m)", &particleStep, 0.1f, 0.5f, 10.0f);
    if (ImGui::TreeNode("Ice spikes (IceSpikeField)"))
    {
        m_IceSpikes.DrawImGui();
        ImGui::TreePop();
    }
}
