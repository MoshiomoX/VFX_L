#pragma once
#include <vector>
#include <string>
#include <memory>
#include "VFXEntry.h"
#include "EntryType.h"

class VFXEffect
{
public:
    void SetName(const std::string& name) { m_Name = name; }
    const std::string& GetName() const { return m_Name; }

    int AddEntry(EntryType type, float startTime = 0.0f, float duration = -1.0f);
    void RemoveEntry(int index);
    VFXEntry* GetEntry(int index);
    int GetEntryCount() const { return static_cast<int>(m_Entries.size()); }

    void Play(const VFXContext& ctx);
    void Stop(const VFXContext& ctx);
    void Update(float dt, const VFXContext& ctx);

    bool IsPlaying() const { return m_IsPlaying; }
    bool IsLooping() const { return m_Loop; }
    void SetLooping(bool loop) { m_Loop = loop; }
   
    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);

    float GetcurrentTime() const { return m_CurrentTime; }
    float GetTotalDuration() const;

private:
    std::string m_Name = "NewEffect";
    std::vector<std::unique_ptr<VFXEntry>> m_Entries;

    bool m_IsPlaying = false;
    bool m_Loop = false;
    float m_CurrentTime = 0.0f;
};