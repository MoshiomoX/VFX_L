// ============================================================
// SpellFxSystem.h
// 魔法の CPU 側の見た目（2026-10-10、アイススパイクから）。
// 判定・弾は GPU（SwarmSystem）だが、地面に 1 本ずつ合わせる・時間差で順に出すなど、
// GPU の範囲の粒子（時間軸が無い）でできない見た目をここで出す。
// WeaponSystem::GetTriggeredFx（今フレームに撃った上級魔法）を見て：
//   アイススパイク → 撃った人の足元から狙った敵の少し奥まで、左右交互の氷のトゲ（IceSpikeField）+ 冷気・欠片の粒子（IceSpike.json）+ 音
// 場面は Init / Update（gameplay の dt）/ Submit（VFXMeshRenderer の Render の前）を呼ぶだけ
// ============================================================
#pragma once
#include "ECS/System/IceSpikeField.h"
#include "ECS/System/WeaponSystem.h"
#include <cstdint>
#include <vector>

class AreaVFXPlayer;
class VFXMeshRenderer;
struct VFXContext;

class SpellFxSystem
{
public:
    void Init(AreaVFXPlayer* player, VFXContext* ctx) { m_Player = player; m_Ctx = ctx; }
    void Update(float dt, const std::vector<WeaponSystem::TriggeredFx>& fx, const IceSpikeField::HeightFn& heightAt);
    void Submit(VFXMeshRenderer& renderer) { m_IceSpikes.Submit(renderer); }
    // アイススパイク 1 本分の見た目（トゲ + 冷気の粒子 + 音）。start = 足元、end = 線の先、scale = 大きさの倍率。
    // Update からと、自動テスト・ImGui の Test から
    void CastIceSpike(const DirectX::SimpleMath::Vector3& start, const DirectX::SimpleMath::Vector3& end, float scale,
        const IceSpikeField::HeightFn& heightAt);
    void Clear() { m_IceSpikes.Clear(); }
    // testStart / testEnd = ImGui の「Test cast」で出す線（場面がプレイヤーの足元から前へ 8m を入れる）
    void DrawImGui(const DirectX::SimpleMath::Vector3& testStart, const DirectX::SimpleMath::Vector3& testEnd, const IceSpikeField::HeightFn& heightAt);

    IceSpikeField& IceSpikes() { return m_IceSpikes; }
    uint32_t IceSpikeCasts() const { return m_IceSpikeCasts; }   // 自動テスト用（撃った回数の累計）

    bool  particles = true;      // 冷気・欠片の粒子（IceSpike.json）
    float particleStep = 3.5f;   // 粒子を線に沿ってこの間隔で出す（m）

private:
    AreaVFXPlayer* m_Player = nullptr;
    VFXContext* m_Ctx = nullptr;
    IceSpikeField m_IceSpikes;
    uint32_t m_IceSpikeCasts = 0;
};
