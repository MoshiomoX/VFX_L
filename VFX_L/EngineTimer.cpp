#include "EngineTimer.h"

void EngineTimer::Start()
{
    m_StartTime = std::chrono::steady_clock::now();
    m_PrevTime = m_StartTime;
}

void EngineTimer::Tick()
{
    auto now = std::chrono::steady_clock::now();
    m_DeltaTime = std::chrono::duration<float>(now - m_PrevTime).count();
    m_PrevTime = now;

    // FPS计算（每秒更新一次）
    m_FrameCount++;
    m_FPSTimer += m_DeltaTime;
    if (m_FPSTimer >= 1.0f)
    {
        m_FPS = static_cast<float>(m_FrameCount) / m_FPSTimer;
        m_FrameCount = 0;
        m_FPSTimer = 0.0f;
    }
}

float EngineTimer::TotalTime() const
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - m_StartTime).count();
}