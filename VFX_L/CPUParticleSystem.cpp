#include "CPUParticleSystem.h"
#include <iostream>

CPUParticleSystem::CPUParticleSystem()
{
}

bool CPUParticleSystem::Initialize(int maxParticles)
{
    m_Particles.resize(maxParticles);
    std::cout << "[OK] ParticleSystem initialized: " << maxParticles << " particles" << std::endl;
    return true;
}

void CPUParticleSystem::Update(float dt)
{
    if (m_IsEmitting)
    {
        Emit(dt);
    }
    UpdateParticles(dt);
}

void CPUParticleSystem::Emit(float dt)
{
    m_EmitAccumulator += m_Emitter.emitRate * dt;

    while (m_EmitAccumulator >= 1.0f)
    {
        int index = FindDeadParticle();
        if (index >= 0)
        {
            m_Emitter.InitParticle(m_Particles[index]);
        }
        m_EmitAccumulator -= 1.0f;
    }
}

void CPUParticleSystem::UpdateParticles(float dt)
{
    for (auto& p : m_Particles)
    {
        if (!p.isAlive)
            continue;

        // 移動
        p.velocity += m_Emitter.gravity * dt;
        p.position += p.velocity * dt;

        // 老化
        p.age += dt;
        if (p.age >= p.lifetime)
        {
            p.isAlive = false;
            continue;
        }

        // 色補間
        float t = p.GetLifeRatio();
        p.color = Vector4::Lerp(m_Emitter.startColor, m_Emitter.endColor, t);
    }
}

int CPUParticleSystem::FindDeadParticle()
{
    for (int i = 0; i < (int)m_Particles.size(); i++)
    {
        if (!m_Particles[i].isAlive)
            return i;
    }
    return -1;  // 全部使用中
}

int CPUParticleSystem::GetAliveCount() const
{
    int count = 0;
    for (const auto& p : m_Particles)
    {
        if (p.isAlive) count++;
    }
    return count;
}

void CPUParticleSystem::Reset()
{
    for (auto& p : m_Particles)
    {
        p.isAlive = false;
    }
    m_EmitAccumulator = 0.0f;
}
