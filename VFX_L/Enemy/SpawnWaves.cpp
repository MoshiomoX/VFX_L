// ============================================================
// SpawnWaves.cpp
// 湧きの波（押し寄せ → 一息 → 普段）。説明は SpawnWaves.h
// ============================================================
#include "Enemy/SpawnWaves.h"
#include "imgui.h"

void SpawnWaves::Reset(uint32_t seed)
{
    m_Rng.seed(seed ^ 0x5A17u);
    m_Phase = Phase::Normal;
    m_PhaseEnd = 0.0f;
    m_NextAt = firstAt;
    m_ArcYaw = 0.0f;
    m_Count = 0;
    m_JustStarted = false;
    m_Debug = false;
}

float SpawnWaves::RateMul() const
{
    if (!enabled) return 1.0f;
    switch (m_Phase)
    {
    case Phase::Surge: return surgeMul;
    case Phase::Lull:  return lullMul;
    default:           return baseMul;
    }
}

void SpawnWaves::StartSurge(float runTime, float stageTime)
{
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    // 序盤は短く、制限時間に近いほど長く
    const float k = (stageTime > 0.0f) ? (std::min)(1.0f, (std::max)(0.0f, runTime / stageTime)) : 0.5f;
    m_Phase = Phase::Surge;
    m_PhaseEnd = runTime + surgeTime + (surgeTimeLate - surgeTime) * k;
    m_ArcYaw = u01(m_Rng) * 6.2831853f;
    m_NextAt = runTime + interval + (u01(m_Rng) * 2.0f - 1.0f) * jitter;
    m_JustStarted = true;
    ++m_Count;
}

void SpawnWaves::Update(float runTime, float stageTime)
{
    m_JustStarted = false;
    if (!enabled)
    {
        m_Phase = Phase::Normal;
        return;
    }

    if (m_Debug)
    {
        m_Debug = false;
        StartSurge(runTime, stageTime);
        return;
    }

    switch (m_Phase)
    {
    case Phase::Surge:
        if (runTime >= m_PhaseEnd)
        {
            m_Phase = Phase::Lull;
            m_PhaseEnd = runTime + lullTime;
        }
        break;
    case Phase::Lull:
        if (runTime >= m_PhaseEnd) m_Phase = Phase::Normal;
        break;
    default:
    {
        const bool tooLate = stageTime > 0.0f && runTime >= stageTime - stopBefore;
        if (!tooLate && runTime >= m_NextAt) StartSurge(runTime, stageTime);
        break;
    }
    }
}

void SpawnWaves::DrawImGui(float runTime)
{
    const char* names[] = { "normal", "SURGE", "lull" };
    ImGui::Text("phase %s (%.1f s left)  surges %d  next at %.0f s",
        names[(int)m_Phase], m_Phase == Phase::Normal ? 0.0f : PhaseLeft(runTime), m_Count, m_NextAt);
    ImGui::Checkbox("Waves##waves", &enabled);
    ImGui::SameLine();
    if (ImGui::Button("Surge Now##waves")) QueueNow();
    ImGui::DragFloat("First At (s)##waves", &firstAt, 0.5f, 0.0f, 600.0f);
    ImGui::DragFloat("Interval (s)##waves", &interval, 0.5f, 5.0f, 600.0f);
    ImGui::DragFloat("Jitter (s)##waves", &jitter, 0.5f, 0.0f, 120.0f);
    ImGui::DragFloat2("Surge Time early / late (s)##waves", &surgeTime, 0.5f, 1.0f, 120.0f);
    ImGui::DragFloat("Surge x##waves", &surgeMul, 0.05f, 1.0f, 10.0f);
    ImGui::DragFloat("Lull Time (s)##waves", &lullTime, 0.5f, 0.0f, 120.0f);
    ImGui::DragFloat("Lull x##waves", &lullMul, 0.05f, 0.0f, 1.0f);
    ImGui::DragFloat("Normal x##waves", &baseMul, 0.01f, 0.1f, 2.0f);
    ImGui::DragFloat("Arc +- (deg)##waves", &arcHalfDeg, 1.0f, 5.0f, 180.0f);
    ImGui::DragFloat("Stop Before Time Up (s)##waves", &stopBefore, 1.0f, 0.0f, 300.0f);
}
