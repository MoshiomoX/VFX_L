#include "ParticleEffect.h"
#include "GPUParticleSystem.h"
#include <algorithm>    
#include <iostream>

int ParticleEffect::AddEntry(float startTime, float duration)
{
    EmitterEntry entry;
    entry.startTime = startTime;
    entry.duration = duration;
    m_Entries.push_back(entry);
    return static_cast<int>(m_Entries.size()) - 1;
}

void ParticleEffect::RemoveEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_Entries.size())) return;
    m_Entries.erase(m_Entries.begin() + index);
}

EmitterEntry* ParticleEffect::GetEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_Entries.size())) return nullptr;
    return &m_Entries[index];
}

float ParticleEffect::GetTotalDuration() const
{
    float maxEnd = 0.0f;
    for (auto& entry : m_Entries)
    {
        float end = (entry.duration < 0.0f)
            ? 9999.0f
            : entry.startTime + entry.duration;
        maxEnd = max(maxEnd, end);
    }
    return maxEnd;
}

void ParticleEffect::Play(GPUParticleSystem* system)
{
    if (!system) return;

    m_CurrentTime = 0.0f;
    m_IsPlaying = true;

    // 全エントリをリセット
    for (auto& entry : m_Entries)
    {
        entry.isPlaying = false;
        entry.runtimeID = -1;
    }

    std::cout << "[ParticleEffect] Play: " << m_Name << std::endl;
}

void ParticleEffect::Stop(GPUParticleSystem* system)
{
    if (!system) return;

    for (auto& entry : m_Entries)
    {
        if (entry.runtimeID >= 0)
        {
            system->RemoveEmitter(entry.runtimeID);
            entry.runtimeID = -1;
        }
        entry.isPlaying = false;
    }

    // アロケータをリセット
    system->ResetAllocator();

    m_IsPlaying = false;
    m_CurrentTime = 0.0f;

    std::cout << "[ParticleEffect] Stop: " << m_Name << std::endl;
}

void ParticleEffect::Update(float dt, GPUParticleSystem* system)
{
    if (!m_IsPlaying || !system) return;

    m_CurrentTime += dt;

    for (auto& entry : m_Entries)
    {
        float endTime = (entry.duration < 0.0f)
            ? 9999.0f
            : entry.startTime + entry.duration;

        // 開始時間に達した → Emitter登録
        if (!entry.isPlaying && m_CurrentTime >= entry.startTime && m_CurrentTime < endTime)
        {
            entry.runtimeID = system->AddEmitter(
                entry.emitterData.emitRate,
                entry.emitterData.maxParticles);

            if (entry.runtimeID >= 0)
            {
                // パラメータをコピー
                auto* emitter = system->GetEmitter(entry.runtimeID);
                if (emitter)
                {
                    emitter->emitType = entry.emitterData.emitType;
                    emitter->shape = entry.emitterData.shape;
                    emitter->position = entry.emitterData.position;
                    emitter->direction = entry.emitterData.direction;
                    emitter->speedRange = entry.emitterData.speedRange;
                    emitter->lifetimeRange = entry.emitterData.lifetimeRange;
                    emitter->sizeRange = entry.emitterData.sizeRange;
                    emitter->startColorMin = entry.emitterData.startColorMin;
                    emitter->startColorMax = entry.emitterData.startColorMax;
                    emitter->endColorMin = entry.emitterData.endColorMin;
                    emitter->endColorMax = entry.emitterData.endColorMax;
                    emitter->gravity = entry.emitterData.gravity;
                    emitter->dragCoeff = entry.emitterData.dragCoeff;
                    emitter->rotationRange = entry.emitterData.rotationRange;
                    emitter->angularVelRange = entry.emitterData.angularVelRange;
                    emitter->atlasRows = entry.emitterData.atlasRows;
                    emitter->atlasCols = entry.emitterData.atlasCols;
                    emitter->atlasIndex = entry.emitterData.atlasIndex;
                    emitter->atlasAnimate = entry.emitterData.atlasAnimate;
                    emitter->textureIndex = entry.emitterData.textureIndex;
                }
                entry.isPlaying = true;
            }
        }

        // 持続時間を超えた → Emitter削除
        if (entry.isPlaying && m_CurrentTime >= endTime)
        {
            if (entry.runtimeID >= 0)
            {
                system->RemoveEmitter(entry.runtimeID);
                entry.runtimeID = -1;
            }
            entry.isPlaying = false;
        }
    }

    // 全エントリ終了チェック
    bool allDone = true;
    for (auto& entry : m_Entries)
    {
        float endTime = (entry.duration < 0.0f) ? 9999.0f : entry.startTime + entry.duration;
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
            Stop(system);
            Play(system);
        }
        else
        {
            Stop(system);
        }
    }
}