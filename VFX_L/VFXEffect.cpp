// ============================================================
// VFXEffect.cpp
// 状態機（HSM）駆動版
// ============================================================
#include "VFXEffect.h"
#include "VFXParticleEntry.h"
#include "GPUParticleSystem.h"
#include <algorithm>
#include <iostream>

#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

// ============================================================
// Entry 管理（既存：変更なし）
// ============================================================

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
        maxEnd = (std::max)(maxEnd, end);
    }
    return maxEnd;
}

// ============================================================
// 状態機から呼ばれるメソッド（既存 Update から切り出し）
// ============================================================

void VFXEffect::AdvanceTime(float dt)
{
    m_CurrentTime += dt;
}

void VFXEffect::UpdateEntries(float dt, const VFXContext& ctx)
{
    for (auto& entry : m_Entries)
    {
        float endTime = (entry->duration < 0.0f)
            ? 9999.0f
            : entry->startTime + entry->duration;

        // 開始時刻に達し、未再生なら再生開始
        if (!entry->isPlaying && m_CurrentTime >= entry->startTime && m_CurrentTime < endTime)
        {
            entry->OnPlay(ctx);
        }

        // 再生中かつ Duration 内なら更新
        if (entry->isPlaying && m_CurrentTime < endTime)
        {
            entry->OnUpdate(dt, ctx);
        }

        // 再生中かつ Duration を超えたら停止
        if (entry->isPlaying && m_CurrentTime >= endTime)
        {
            entry->OnStop(ctx);
        }
    }
}

void VFXEffect::CollectAndDispatch(float dt, const VFXContext& ctx)
{
    // Emitter データ収集
    std::vector<GPUEmitter> emitters;
    std::vector<ColorKey> colorKeys;
    int colorKeyOffset = 0;

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

    // GPU に送信（発射 + 粒子更新）
    if (ctx.particleSystem)
    {
        ctx.particleSystem->Update(dt, m_CurrentTime, emitters, colorKeys);
    }
}

void VFXEffect::DispatchUpdateOnly(float dt, const VFXContext& ctx)
{
    // 空の emitters を渡す → 新規発射0、UpdateCS だけ走る（自然消滅）
    std::vector<GPUEmitter> emptyEmitters;
    std::vector<ColorKey> emptyKeys;
    if (ctx.particleSystem)
    {
        ctx.particleSystem->Update(dt, m_CurrentTime, emptyEmitters, emptyKeys);
    }
}

void VFXEffect::StopAllEntries(const VFXContext& ctx)
{
    for (auto& entry : m_Entries)
    {
        if (entry->isPlaying)
        {
            entry->OnStop(ctx);
        }
    }
}

bool VFXEffect::IsAllEntriesDone() const
{
    for (auto& entry : m_Entries)
    {
        float endTime = (entry->duration < 0.0f)
            ? 9999.0f
            : entry->startTime + entry->duration;
        if (m_CurrentTime < endTime)
        {
            return false;
        }
    }
    return true;
}

void VFXEffect::ResetTimeline()
{
    m_CurrentTime = 0.0f;
    for (auto& entry : m_Entries)
    {
        entry->isPlaying = false;
    }
}

uint32_t VFXEffect::GetAliveCount(const VFXContext& ctx) const
{
    if (ctx.particleSystem)
        return ctx.particleSystem->GetAliveCount();
    return 0;
}

// ============================================================
// 状態機駆動（新 API）
// ============================================================

void VFXEffect::InitStateMachine(const VFXContext& ctx)
{
    RegisterVFXStates(m_SM);
    m_SMCtx.vfxCtx = const_cast<VFXContext*>(&ctx);
    m_SM.Start(VFXStateID::Idle, m_SMCtx, *this);
}

void VFXEffect::Update(float dt)
{
    m_SM.Update(m_SMCtx, *this, dt);
}

void VFXEffect::Play()
{
    // 即時に Idle → Playing で確実に最初から再生
    m_SM.ChangeState(m_SMCtx, *this, VFXStateID::Idle);
    ResetTimeline();
    m_SM.ChangeState(m_SMCtx, *this, VFXStateID::Playing);
}

void VFXEffect::Stop()
{
    m_SM.SendEvent(VFXStateID::Finishing);
}

// ============================================================
// SetLooping（状態機版：Playing 中に Loop OFF → Finishing へ遷移要求）
// ============================================================

void VFXEffect::SetLooping(bool loop)
{
    if (m_Loop && !loop)
    {
        // Loop 中に OFF にされた → 現在 Playing なら Finishing へ
        if (m_SMCtx.current == VFXStateID::Playing)
        {
            m_SM.SendEvent(VFXStateID::Finishing);
        }
    }
    m_Loop = loop;
}

// ============================================================
// セーブ / ロード（既存：ほぼ変更なし）
// ============================================================

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

    // ロード後は Idle 状態にリセット
    m_CurrentTime = 0.0f;
    m_SMCtx.current = VFXStateID::Idle;
    m_SMCtx.timeInState = 0.0f;

    std::cout << "[OK] Loaded: " << filepath << std::endl;
    return true;
}