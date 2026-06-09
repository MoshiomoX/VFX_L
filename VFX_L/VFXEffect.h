// VFXEffect.h
#pragma once

#include <vector>
#include <string>
#include <memory>
#include "VFXEntry.h"
#include "EntryType.h"
#include "VFXStates.h"  // ← 追加（VFXStateMachine, VFXStateContext の定義）

class VFXEffect
{
public:
    // --- 既存（変更なし）---
    void SetName(const std::string& name) { m_Name = name; }
    const std::string& GetName() const { return m_Name; }
    int AddEntry(EntryType type, float startTime = 0.0f, float duration = -1.0f);
    void RemoveEntry(int index);
    VFXEntry* GetEntry(int index);
    int GetEntryCount() const { return static_cast<int>(m_Entries.size()); }
    bool IsLooping() const { return m_Loop; }
    void SetLooping(bool loop);
    float GetcurrentTime() const { return m_CurrentTime; }
    float GetTotalDuration() const;
    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);

    // --- 状態機から呼ばれるメソッド（既存 Update から切り出し）---
    void AdvanceTime(float dt);
    void UpdateEntries(float dt, const VFXContext& ctx);
    void CollectAndDispatch(float dt, const VFXContext& ctx);
    void DispatchUpdateOnly(float dt, const VFXContext& ctx);
    void StopAllEntries(const VFXContext& ctx);
    bool IsAllEntriesDone() const;
    void ResetTimeline();
    uint32_t GetAliveCount(const VFXContext& ctx) const;

    // --- 状態機駆動（新しい API）---
    void InitStateMachine(const VFXContext& ctx);
    void Update(float dt);                    // 新：状態機駆動版
    void Play();                               // 新：Event 駆動
    void Stop();                               // 新：Event 駆動

    // --- 互換用（VFXEditor がそのまま使えるように）---
    bool IsPlaying() const { return m_SMCtx.current == VFXStateID::Playing; }
    bool IsFinishing() const { return m_SMCtx.current == VFXStateID::Finishing; }

private:
    std::string m_Name = "NewEffect";
    std::vector<std::unique_ptr<VFXEntry>> m_Entries;
    bool m_Loop = false;
    float m_CurrentTime = 0.0f;

    // --- 状態機（追加）---
    VFXStateMachine m_SM;   
    VFXStateContext m_SMCtx;

};