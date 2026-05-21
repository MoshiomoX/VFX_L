#include "VFXEffect.h"
#include "VFXParticleEntry.h"
#include "GPUParticleSystem.h"
#include <algorithm>
#include <iostream>

#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

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
bool VFXEffect::SaveToFile(const std::string& filepath) const
{
    json root;
    root["name"] = m_Name;
    root["loop"] = m_Loop;

    json entries = json::array();
    const char* typeNames[] = { "Particle", "Sprite", "Trail", "Mesh", "Light", "Sound" };

    for (auto& entry : m_Entries)
    {
        json e;
        int typeIdx = static_cast<int>(entry->GetType());
        e["type"] = typeNames[typeIdx];
        e["startTime"] = entry->startTime;
        e["duration"] = entry->duration;
        e["data"] = entry->ToJson();
        entries.push_back(e);
    }

    root["entries"] = entries;

    std::ofstream file(filepath);
    if (!file.is_open())
    {
        std::cout << "[Error] Failed to save: " << filepath << std::endl;
        return false;
    }

    file << root.dump(4);
    std::cout << "[OK] Saved: " << filepath << std::endl;
    return true;
}
bool VFXEffect::LoadFromFile(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cout << "[Error] Failed to load: " << filepath << std::endl;
        return false;
    }

    json root;
    try
    {
        file >> root;
    }
    catch (const json::exception& e)
    {
        std::cout << "[Error] JSON parse error: " << e.what() << std::endl;
        return false;
    }

    m_Entries.clear();

    // nameがJSONにあればそれを使う、なければファイル名
    if (root.contains("name"))
    {
        m_Name = root["name"];
    }
    else
    {
        size_t lastSlash = filepath.find_last_of("/\\");
        size_t lastDot = filepath.find_last_of('.');
        size_t start = (lastSlash != std::string::npos) ? lastSlash + 1 : 0;
        if (lastDot != std::string::npos && lastDot > start)
            m_Name = filepath.substr(start, lastDot - start);
        else
            m_Name = "NewEffect";
    }

    m_Loop = root.value("loop", false);

    if (root.contains("entries"))
    {
        for (auto& e : root["entries"])
        {
            std::string type = e.value("type", "Particle");
            float st = e.value("startTime", 0.0f);
            float dur = e.value("duration", -1.0f);

            EntryType entryType = EntryType::Particle;
            if (type == "Sprite") entryType = EntryType::Sprite;
            else if (type == "Trail") entryType = EntryType::Trail;
            else if (type == "Mesh") entryType = EntryType::Mesh;
            else if (type == "Light") entryType = EntryType::Light;
            else if (type == "Sound") entryType = EntryType::Sound;

            int idx = AddEntry(entryType, st, dur);
            if (idx >= 0 && e.contains("data"))
            {
                m_Entries[idx]->FromJson(e["data"]);
            }
        }
    }

    m_IsPlaying = false;
    m_CurrentTime = 0.0f;

    std::cout << "[OK] Loaded: " << filepath << std::endl;
    return true;
}