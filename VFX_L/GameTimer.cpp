#include "GameTimer.h"

void GameTimer::Start(float duration)
{
    m_Duration = duration;
    m_CurrentTime = 0.0f;
    m_IsRunning = true;
    m_IsPaused = false;
}

void GameTimer::Update(float deltaTime)
{
    if (!m_IsRunning || m_IsPaused) return;

    m_CurrentTime += deltaTime;
}

void GameTimer::Pause()
{
    m_IsPaused = true;
}

void GameTimer::Resume()
{
    m_IsPaused = false;
}

void GameTimer::Stop()
{
    m_IsRunning = false;
    m_IsPaused = false;
}

void GameTimer::Reset()
{
    m_CurrentTime = 0.0f;
}

bool GameTimer::IsElapsed() const
{
    return m_IsRunning && (m_CurrentTime >= m_Duration);
}

float GameTimer::GetProgress() const
{
    if (m_Duration <= 0.0f) return 0.0f;
    float progress = m_CurrentTime / m_Duration;
    return (progress > 1.0f) ? 1.0f : progress;
}

float GameTimer::GetRemaining() const
{
    float remaining = m_Duration - m_CurrentTime;
    return (remaining < 0.0f) ? 0.0f : remaining;
}