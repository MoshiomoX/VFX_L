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
    m_StopRequested = false;

    for (auto& entry : m_Entries)
    {
        entry->isPlaying = false;
    }
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

    m_IsPlaying = false;
    m_CurrentTime = 0.0f;
}
void VFXEffect::Update(float dt, const VFXContext& ctx)
{
    if (!m_IsPlaying) return;

    m_CurrentTime += dt;

    // Entry状態更新
    for (auto& entry : m_Entries)
    {
        float endTime = (entry->duration < 0.0f)
            ? 9999.0f
            : entry->startTime + entry->duration;

        if (!entry->isPlaying && m_CurrentTime >= entry->startTime && m_CurrentTime < endTime)
        {
            if (!m_Finishing)  // 終了中は新規Playしない
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

    // Emitterデータ収集（終了中は空のemittersを渡す → 発射0、Updateだけ走る）
    std::vector<GPUEmitter> emitters;
    std::vector<ColorKey> colorKeys;
    int colorKeyOffset = 0;

    if (!m_Finishing)
    {
        for (auto& entry : m_Entries)
        {
            if (entry->GetType() == EntryType::Particle && entry->isPlaying)
            {
                auto* pEntry = static_cast<VFXParticleEntry*>(entry.get());
                pEntry->emitterData.Update(dt);
                GPUEmitter ge = pEntry->emitterData.ToGPU();
                ge.colorKeyOffset = colorKeyOffset;
                emitters.push_back(ge);

                for (int k = 0; k < pEntry->emitterData.colorKeyCount; k++)
                {
                    colorKeys.push_back(pEntry->emitterData.colorKeys[k]);
                }
                colorKeyOffset += pEntry->emitterData.colorKeyCount;
            }
        }
    }

    // Systemに渡す（m_Finishing中も呼ぶ → UpdateCSが走り粒子が自然消滅する）
    if (ctx.particleSystem)
    {
        ctx.particleSystem->Update(dt, m_CurrentTime, emitters, colorKeys);
    }

    // allDone判定
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
        if (m_Loop && !m_Finishing)
        {
            m_CurrentTime = 0.0f;
            for (auto& entry : m_Entries)
            {
                entry->isPlaying = false;
            }
        }
        else if (m_Finishing)
        {
            // 全粒子が消滅したか確認
            if (ctx.particleSystem && ctx.particleSystem->GetAliveCount() == 0)
            {
                Stop(ctx);
                m_Finishing = false;
                m_StopRequested = false;
            }
            // まだ生きてる粒子がある → UpdateCSだけ回し続ける
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

void VFXEffect::SetLooping(bool loop)
{
    if (m_Loop && !loop)
    {
        // Loop中にOFFにした場合
        m_Finishing = true;
    }
    m_Loop = loop;
}