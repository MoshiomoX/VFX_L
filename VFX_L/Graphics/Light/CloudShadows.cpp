// ============================================================
// CloudShadows.cpp
// ============================================================
#include "Graphics/Light/CloudShadows.h"
#include "Graphics/Renderer/Renderer.h"
#include "World/WindState.h"

#include "imgui.h"

using namespace DirectX::SimpleMath;

void CloudShadows::SetBiome(int biome)
{
    if (biome == m_Biome) return;   // 同じ面ならパネルで変えた値を残す
    m_Biome = biome;
    const bool enabled = m_S.enabled;
    m_S = Settings{};
    m_S.enabled = enabled;
    if (biome == 1)        // 砂漠：雲は少なく小さめ
    {
        m_S.coverage = 0.32f;
        m_S.size = 26.0f;
        m_S.strength = 0.45f;
    }
    else if (biome == 2)   // 遺跡：もともと暗いので薄く
    {
        m_S.strength = 0.3f;
    }
}

void CloudShadows::Update(const WindState& wind, float time)
{
    if (m_LastTime < 0.0f || time < m_LastTime)
        m_LastTime = time;
    const float dt = (std::min)(time - m_LastTime, 0.25f);
    m_LastTime = time;
    m_Offset += wind.dir * (wind.speed * wind.strength * m_S.speedMul * dt);
    // 浮動小数の精度のための巻き戻し（秒速 5m でも 5 時間以上かかる）
    constexpr float kWrap = 100000.0f;
    if (std::abs(m_Offset.x) > kWrap || std::abs(m_Offset.y) > kWrap) m_Offset = Vector2(0.0f, 0.0f);
}

void CloudShadows::Apply(Renderer& renderer) const
{
    if (!m_S.enabled || m_S.strength <= 0.0f)
    {
        renderer.SetCloudShadow(Vector4(0, 0, 0, 0), Vector4(0, 0, 0, 0));
        return;
    }
    renderer.SetCloudShadow(
        Vector4(m_S.strength, 1.0f / (std::max)(m_S.size, 1.0f), m_S.coverage, m_S.softness),
        Vector4(m_Offset.x, m_Offset.y, 0.0f, 0.0f));
}

void CloudShadows::Clear(Renderer& renderer)
{
    renderer.SetCloudShadow(Vector4(0, 0, 0, 0), Vector4(0, 0, 0, 0));
}

void CloudShadows::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Cloud Shadows")) return;
    ImGui::Checkbox("Enabled##clouds", &m_S.enabled);
    ImGui::SliderFloat("Strength##clouds", &m_S.strength, 0.0f, 1.0f);
    ImGui::DragFloat("Size (m)##clouds", &m_S.size, 0.5f, 4.0f, 300.0f);
    ImGui::SliderFloat("Coverage##clouds", &m_S.coverage, 0.0f, 1.0f);
    ImGui::SliderFloat("Softness##clouds", &m_S.softness, 0.0f, 0.4f);
    ImGui::DragFloat("Speed x wind##clouds", &m_S.speedMul, 0.05f, 0.0f, 20.0f);
    ImGui::Text("offset %.1f, %.1f", m_Offset.x, m_Offset.y);
}
