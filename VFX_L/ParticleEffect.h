#pragma once
#include <vector>
#include <string>
#include "GPUParticleEmitter.h"

class GPUParticleSystem;

// ============================================
// タイムライン上の1エントリ
// Emitterパラメータ + 開始時間 + 持続時間
// ============================================
struct EmitterEntry
{
    GPUParticleEmitter emitterData;
    float startTime = 0.0f;
    float duration = -1.0f;      // -1 = 無限
    int runtimeID = -1;          // GPUParticleSystemに登録後のID
    bool isPlaying = false;      // 現在再生中か
};

// ============================================
// ParticleEffect
// 複数Emitterのタイムライン管理
// ============================================
class ParticleEffect
{
public:
    // 名前
    void SetName(const std::string& name) { m_Name = name; }
    const std::string& GetName() const { return m_Name; }

    // Emitter管理
    int AddEntry(float startTime = 0.0f, float duration = -1.0f);
    void RemoveEntry(int index);
    EmitterEntry* GetEntry(int index);
    int GetEntryCount() const { return static_cast<int>(m_Entries.size()); }

    // 再生制御
    void Play(GPUParticleSystem* system);
    void Stop(GPUParticleSystem* system);
    void Update(float dt, GPUParticleSystem* system);

    // 状態
    bool IsPlaying() const { return m_IsPlaying; }
    bool IsLooping() const { return m_Loop; }
    void SetLooping(bool loop) { m_Loop = loop; }
    float GetCurrentTime() const { return m_CurrentTime; }
    float GetTotalDuration() const;

private:
    std::string m_Name = "NewEffect";
    std::vector<EmitterEntry> m_Entries;

    bool m_IsPlaying = false;
    bool m_Loop = false;
    float m_CurrentTime = 0.0f;
};