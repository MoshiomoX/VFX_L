// ============================================================
// VFXLiquidEntry.h
// Liquid entry：地面に溜まる液体を 1 つ描く（2026-10-02）。
//   見た目は VFXLiquidDef（色・光り方・泡・飛び散り方・時間）。
//   CPU：effect の位置 + offset に、entry の duration の間だけ出す（終わりの dryTime で縮む）。
//        向きは previewDir（0 なら seed で決めた角度）。描画は VFXLiquidRenderer。
//   GPU：弾の命中・着弾で生まれた範囲のレシピにこの entry があると、範囲が生きている間
//        その下に描く（SwarmVFXTable → SwarmLiquidVS）。向きは弾が飛んできた方向、
//        半径は def.radius（0 なら範囲の半径）、乾くのは範囲が消える前の dryTime 秒。
//        GPU では startTime / duration は使わない
// ============================================================
#pragma once
#include "VFX_Editor/VFXEntry.h"
#include "VFX_Editor/VFXLiquidDef.h"

class VFXLiquidRenderer;

class VFXLiquidEntry : public VFXEntry
{
public:
    EntryType GetType() const override { return EntryType::Liquid; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;
    std::unique_ptr<VFXEntry> Clone() const override;
    json ToJson() const override;
    void FromJson(const json& j) override;

    // VFXEffect::CollectAndDispatch から毎フレーム
    void Submit(VFXLiquidRenderer& renderer, const DirectX::SimpleMath::Vector3& worldOffset);

    // ---- 設定 ----
    VFXLiquidDef def;
    DirectX::SimpleMath::Vector3 offset = { 0, 0, 0 };       // 中心のずらし（effect の位置から）
    DirectX::SimpleMath::Vector3 previewDir = { 0, 0, 1 };   // CPU で描く時の「投げた向き」（xz）。0 = seed で決めた角度

private:
    float    m_Age = 0.0f;
    uint32_t m_Seed = 0;
};
