#include "VFXEffect.h"
#include "VFXParticleEntry.h"
#include "GPUParticleSystem.h"
#include <algorithm>
#include <iostream>

int VFXEffect::AddEntry(EntryType type, float startTime, float duration)
{
    std::unique_ptr<VFXEntry> entry;

    switch (type)
    {
    case EntryType::Particle:
        entry = std::make_unique<VFXParticleEntry>();
        break;
    default:
        return -1;
    }

    entry->startTime = startTime;
    entry->duration = duration;
    m_Entries.push_back(std::move(entry));
    return static_cast<int>(m_Entries.size()) - 1;
}

void VFXEffect::RemoveEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_Entries.size())) return;
    m_Entries.erase(m_Entries.begin() + index);
}

VFXEntry* VFXEffect::GetEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_Entries.size())) return nullptr;
    return m_Entries[index].get();
}

float VFXEffect::GetTotalDuration() const
{
    float maxEnd = 0.0f;
    for (auto& entry : m_Entries)
    {
        float end = (entry->duration < 0.0f)
            ? 9999.0f
            : entry->startTime + entry->duration;
        maxEnd = max(maxEnd, end);
    }
    return maxEnd;
}

void VFXEffect::Play(const VFXContext& ctx)
{
    m_CurrentTime = 0.0f;
    m_IsPlaying = true;

    for (auto& entry : m_Entries)
    {
        entry->isPlaying = false;
    }

   // std::cout << "[VFXEffect] Play: " << m_Name << std::endl;
}

void VFXEffect::Stop(const VFXContext& ctx)
{
    for (auto& entry : m_Entries)
    {
        if (entry->isPlaying)
        {
            entry->OnStop(ctx);
        }
    }

    if (ctx.particleSystem)
        ctx.particleSystem->ResetAllocator();

    m_IsPlaying = false;
    m_CurrentTime = 0.0f;

 //   std::cout << "[VFXEffect] Stop: " << m_Name << std::endl;
}

void VFXEffect::Update(float dt, const VFXContext& ctx)
{
    if (!m_IsPlaying) return;

    m_CurrentTime += dt;

    for (auto& entry : m_Entries)
    {
        float endTime = (entry->duration < 0.0f)
            ? 9999.0f
            : entry->startTime + entry->duration;

        if (!entry->isPlaying && m_CurrentTime >= entry->startTime && m_CurrentTime < endTime)
        {
            entry->OnPlay(ctx);
        }

        if (entry->isPlaying && m_CurrentTime < endTime)
        {
            entry->OnUpdate(dt, ctx);
        }

        if (entry->isPlaying && m_CurrentTime >= endTime)
        {
            entry->OnStop(ctx);
        }
    }

    bool allDone = true;
    for (auto& entry : m_Entries)
    {
        float endTime = (entry->duration < 0.0f) ? 9999.0f : entry->startTime + entry->duration;
        if (m_CurrentTime < endTime)
        {
            allDone = false;
            break;
        }
    }

    if (allDone)
    {
        if (m_Loop)
        {
            Stop(ctx);
            Play(ctx);
        }
        else
        {
            Stop(ctx);
        }
    }
}