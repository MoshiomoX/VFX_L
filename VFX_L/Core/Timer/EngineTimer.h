#pragma once
#include <chrono>

class EngineTimer
{
public:
    void Start();
    void Tick();

    float DeltaTime() const { return m_DeltaTime; }
    float TotalTime() const;
    float GetFPS() const { return m_FPS; }

private:
    std::chrono::steady_clock::time_point m_StartTime;
    std::chrono::steady_clock::time_point m_PrevTime;

    float m_DeltaTime = 0.0f;
    float m_FPS = 0.0f;

    // FPS计算用
    int m_FrameCount = 0;
    float m_FPSTimer = 0.0f;
};