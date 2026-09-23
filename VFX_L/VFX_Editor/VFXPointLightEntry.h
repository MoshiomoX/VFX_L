#pragma once
#include "VFX_Editor/VFXEntry.h"
#include <memory>

// ============================================================
// Light entry: 点光源。特効の位置（+ offset）に 1 灯置く。
//   寿命中の変化は intensity start → end の線形、
//   flicker で ±揺らぎ（火の玉らしさ）。
//
//   CPU で再生される effect（範囲攻撃・精英・編集器）は
//   VFXEffect::CollectAndDispatch から毎フレーム PointLightManager へ積む。
//   GPU の弾・範囲は SwarmVFXTable がこの entry を光源表に写し、
//   SwarmLightCollectCS が同じリストへ追記する（時間軸は無視される）
// ============================================================
class VFXPointLightEntry : public VFXEntry
{
public:
    EntryType GetType() const override { return EntryType::Light; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;
    std::unique_ptr<VFXEntry> Clone() const override;
    json ToJson() const override;
    void FromJson(const json& j) override;

    // 今フレームの光を PointLightManager へ積む（再生中のみ）
    void Submit(const DirectX::SimpleMath::Vector3& worldOffset);

    DirectX::SimpleMath::Vector3 offset = { 0, 0, 0 };
    DirectX::SimpleMath::Vector3 color = { 1.0f, 0.6f, 0.2f };
    float radius = 4.0f;
    float intensityStart = 3.0f;
    float intensityEnd = 3.0f;      // duration<0 なら start のまま
    float flicker = 0.0f;           // 0〜1。intensity に掛ける揺らぎの幅
    float flickerSpeed = 12.0f;     // Hz 程度

private:
    float Progress() const;
    float m_Age = 0.0f;
};
