#include "Core/Timer/EngineTimer.h"

void EngineTimer::Start()
{
    m_StartTime = std::chrono::steady_clock::now();
    m_PrevTime = m_StartTime;
}

void EngineTimer::Tick()
{
    auto now = std::chrono::steady_clock::now();
    const float realDt = std::chrono::duration<float>(now - m_PrevTime).count();
    m_PrevTime = now;

    // シーン切替やモデル読込で 1 フレームが数秒になる事がある。
    // そのまま物理に渡すと重力 × dt で床を突き抜ける（骨付きモデルの読込で実際に落ちた）ので上限を掛ける
    m_DeltaTime = (realDt > kMaxDeltaTime) ? kMaxDeltaTime : realDt;

    // FPS（1 秒の平均）は上限を掛ける前の本当の間隔で数える（掛けた後の dt だと 10 fps を割っても 10 以上に見える）
    m_FrameCount++;
    m_FPSTimer += realDt;
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