// VFXEffect.h
#pragma once

#include <vector>
#include <string>
#include <memory>
#include "VFX_Editor/VFXEntry.h"
#include "VFX_Editor/EntryType.h"
#include "VFX_Editor/VFXStates.h"  // ← 追加（VFXStateMachine, VFXStateContext の定義）

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

    // --- ステートマシンから呼ばれるメソッド（既存 Update から切り出し）---
    void AdvanceTime(float dt);
    void UpdateEntries(float dt, const VFXContext& ctx);
    void CollectAndDispatch(float dt, const VFXContext& ctx);
    void DispatchUpdateOnly(float dt, const VFXContext& ctx);
    void StopAllEntries(const VFXContext& ctx);
    bool IsAllEntriesDone() const;
    void ResetTimeline();
    uint32_t GetAliveCount(const VFXContext& ctx) const;
    // Finishing（発射を止めた後）で待つ秒数（2026-10-25）。粒子池全体の生存数は CPU に分からないので、
    // この effect が出した物が消え切るまでの時間 = 一番長い粒子の寿命（帯付きなら + 帯の長さ）・Trail entry の長さ + 余裕
    float GetFinishWait() const;

    // 一時停止中用（2026-10-25）：時間を進めず、粒子も出さず、Mesh / Sprite / 光線 / 液溜まり / 点光源を
    // 今の状態のままもう一度積む。各 Renderer と PointLightManager は毎フレーム空になるので、
    // Update を呼ばないフレームに呼ぶと見た目が消えない。Playing の時だけ何かする
    void SubmitStill();

    // --- ステートマシン駆動（新しい API）---
    void InitStateMachine(const VFXContext& ctx);
    void Update(float dt);                    // 新：ステートマシン駆動版
    void Play();                               // 新：Event 駆動
    void Stop();                               // 新：Event 駆動

    // --- 互換用（VFXEditor がそのまま使えるように）---
    bool IsPlaying() const { return m_SMCtx.current == VFXStateID::Playing; }
    bool IsFinishing() const { return m_SMCtx.current == VFXStateID::Finishing; }


    // テンプレートからインスタンスを複製する（entries を深くコピー）
    void CloneFrom(const VFXEffect& src);
    // インスタンスを Y 軸回りに回す（粒子の発射位置・向き、点光源・Mesh・Sprite・Trail・光線・液溜まりのずらし、
    // Mesh の yaw、光線・液溜まりのプレビューの向き）。複製した直後に 1 回
    void RotateYaw(float degrees);
    // インスタンス全体を s 倍の大きさにする（粒子の発射位置・形・速さ・大きさ・重力、Mesh の位置と縮尺、
    // 点光源の位置と半径、Sprite の大きさと位置）。複製した直後に 1 回（拡大鏡で広がった範囲の見た目。2026-10-22）
    void Scale(float s);
    // 色を掛ける（粒子の開始 / 終了色、点光源の色、Mesh entry の tint）。複製した直後に 1 回（水晶玉の光球。2026-10-06）
    void Tint(const DirectX::SimpleMath::Vector3& c) { for (auto& e : m_Entries) e->Tint(c); }

    // 全 emitter に加算されるワールド位置（投射物追従用）
    void SetWorldOffset(const DirectX::SimpleMath::Vector3& p) { m_WorldOffset = p; }

    // 掃引発射：前フレームの位置との間を粒子で埋める。
    // 速い追従でも軌跡が点々にならない。止まっている effect には影響しない
    void SetSweepEnabled(bool v) { m_SweepEnabled = v; }
    bool IsSweepEnabled() const { return m_SweepEnabled; }
    // 瞬間移動の直後に呼ぶ。次のフレームは前の位置と繋がない：
    //   粒子の掃引発射をしない（移動前後を粒子の線で結ばないため）
    void NotifyTeleport() { m_HasPrevOffset = false; }
    void SetGPUTimelineIgnored(bool v) { m_GPUTimelineIgnored = v; }
    // 光線（Beam entry）の終点。入っていなければ entry のプレビュー方向に描く。毎フレーム入れ直す物
    void SetBeamEnd(const DirectX::SimpleMath::Vector3& p) { m_BeamEnd = p; m_HasBeamEnd = true; }
    // 光線の太さの倍率（Beam entry の width に掛ける。拡大鏡で当たり判定が太くなった分、見た目も太くする。2026-10-13）
    void SetBeamWidthMul(float m) { m_BeamWidthMul = m; }
    // 出現 / 消える途中の度合い 0〜1（2026-10-22、竜巻。ユーザー「出る時と消える時が急すぎる」）。入れた値がそのまま使われ続ける。
    // duration < 0 の（ずっと出ている）entry にだけ効く：Mesh = 足元から伸び上がる / ほどける（VFXMeshEntry::presence）、
    // 粒子 = 発射数 × k、点光源 = 強さ × k。出た瞬間だけの entry（duration ≥ 0）は自分の時間軸どおり
    void SetPresence(float k, bool leaving) { m_Presence = k; m_PresenceLeaving = leaving; }
    bool IsGPUTimelineIgnored() const { return m_GPUTimelineIgnored; }
private:
    // 粒子以外の見た目（Mesh / Sprite / 光線 / 液溜まり / 点光源）を各描画先へ積む。CollectAndDispatch と SubmitStill の共通部分
    void SubmitDrawables(const VFXContext& ctx);

    std::string m_Name = "NewEffect";
    std::vector<std::unique_ptr<VFXEntry>> m_Entries;
    bool m_Loop = false;
    float m_CurrentTime = 0.0f;
    DirectX::SimpleMath::Vector3 m_WorldOffset = { 0, 0, 0 };
    DirectX::SimpleMath::Vector3 m_PrevWorldOffset = { 0, 0, 0 };   // 掃引発射用
    bool m_HasPrevOffset = false;   // 再生直後・瞬間移動直後は false
    bool m_SweepEnabled = true;
    bool m_GPUTimelineIgnored = false;
    DirectX::SimpleMath::Vector3 m_BeamEnd = { 0, 0, 0 };
    bool m_HasBeamEnd = false;
    float m_BeamWidthMul = 1.0f;
    float m_Presence = 1.0f;
    bool m_PresenceLeaving = false;
    // --- ステートマシン（追加）---
    VFXStateMachine m_SM;   
    VFXStateContext m_SMCtx;
};
