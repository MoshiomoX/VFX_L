// ============================================================
// BossIntro.cpp
// ============================================================
#include "Enemy/Boss/BossIntro.h"

void BossIntro::Start(const Vector3& gate, const Vector3& walkDir, const Vector3& spawn)
{
    m_Gate = gate;
    m_Spawn = spawn;
    m_Dir = Vector3(walkDir.x, 0.0f, walkDir.z);
    if (m_Dir.LengthSquared() < 1e-6f) m_Dir = Vector3(0.0f, 0.0f, 1.0f);
    m_Dir.Normalize();
    m_Phase = Phase::Rumble;
    m_Time = 0.0f;
    m_StartedFrame = true;
}

void BossIntro::Update(float dt, bool bossAlive, Frame& out)
{
    out = Frame{};
    if (m_Phase == Phase::Idle) return;
    out.active = true;
    out.dir = DirectX::SimpleMath::Vector2(m_Dir.x, m_Dir.z);
    if (m_StartedFrame)
    {
        out.rumbleStart = true;
        m_StartedFrame = false;
    }
    m_Time += dt;

    switch (m_Phase)
    {
    case Phase::Rumble:
        // 門が荒れている間は弱く揺れ続ける（だんだん強く）
        out.shake = 0.12f + 0.18f * (std::min)(m_Time, 1.0f);
        if (bossAlive)
        {
            m_Phase = Phase::Walk;
            m_Time = 0.0f;
            out.emerge = true;
            out.shake = 0.45f;
        }
        else if (m_Time > maxWaitSpawn + 1.0f)
        {
            m_Phase = Phase::Idle;   // 湧かなかった（StageDirector が門の周りに湧かせ直す。演出は諦める）
            out.finished = true;
        }
        break;
    case Phase::Walk:
        out.steer = true;
        out.speed = walkSpeed;
        if (m_Time >= walkTime || !bossAlive)
        {
            m_Phase = Phase::Hold;
            m_Time = 0.0f;
            out.speed = 0.0f;
            out.stomp = bossAlive;
            out.shake = 0.55f;
        }
        break;
    case Phase::Hold:
        out.steer = true;   // 踏み込みの後は少しその場に立つ
        out.speed = 0.0f;
        if (m_Time >= holdTime || !bossAlive)
        {
            m_Phase = Phase::Idle;
            out.finished = true;
            out.steer = false;
        }
        break;
    default:
        break;
    }
}
