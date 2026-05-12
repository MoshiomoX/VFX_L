#pragma once

class GameTimer
{
public:
    void Start(float duration);
    void Update(float deltaTime);
    void Pause();
    void Resume();
    void Stop();
    void Reset();

    bool IsRunning() const { return m_IsRunning; }
    bool IsElapsed() const;
    float GetProgress() const;      // 0.0 ~ 1.0
    float GetRemaining() const;     // Žc‚èŽžŠÔ
    float GetElapsed() const { return m_CurrentTime; }

private:
    float m_Duration = 0.0f;        // –Ú•WŽžŠÔ
    float m_CurrentTime = 0.0f;     // Œo‰ßŽžŠÔ
    bool m_IsRunning = false;
    bool m_IsPaused = false;
};