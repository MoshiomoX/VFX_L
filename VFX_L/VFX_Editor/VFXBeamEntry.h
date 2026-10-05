// ============================================================
// VFXBeamEntry.h
// Beam entry：起点 → 終点の光線を 1 本描く（2026-09-30）。
//   起点 = effect の位置 + offset。終点は effect の外から毎フレーム入る
//   （VFXEffect::SetBeamEnd。戦闘では WeaponSystem が光線の当たり判定と同じ物を入れる）。
//   入っていない時（VFX エディタのプレビュー）は previewDir へ previewLength m。
//   3 層（グロー / 主色 / 白芯）、長さ方向に流れるノイズ、出る時は根元から先端へ伸び、
//   entry の duration の終わりは根元から縮んで消える。描画は VFXBeamRenderer（CPU）。
//   GPU の範囲（弾の命中で生まれた物）では描かれない
// ============================================================
#pragma once
#include "VFX_Editor/VFXEntry.h"

class VFXBeamRenderer;

class VFXBeamEntry : public VFXEntry
{
public:
    EntryType GetType() const override { return EntryType::Beam; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;
    std::unique_ptr<VFXEntry> Clone() const override;
    json ToJson() const override;
    void FromJson(const json& j) override;

    // VFXEffect::CollectAndDispatch から毎フレーム。hasEnd = false なら previewDir / previewLength
    void Submit(VFXBeamRenderer& renderer, const DirectX::SimpleMath::Vector3& worldOffset,
        const DirectX::SimpleMath::Vector3& beamEnd, bool hasEnd);

    // ---- 設定 ----
    DirectX::SimpleMath::Vector3 offset = { 0, 0, 0 };        // 起点のずらし（effect の位置から）
    float width = 0.8f;                                        // 主色の全幅（m）
    float coreRatio = 0.35f;                                   // 白芯の幅（主色比）
    float glowRatio = 2.2f;                                    // グローの幅（主色比）
    float glowAlpha = 0.35f;
    DirectX::SimpleMath::Vector4 color = { 0.3f, 0.8f, 1.0f, 1.0f };       // 主色（HDR。1 を超えると bloom で光る）
    DirectX::SimpleMath::Vector4 coreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float scrollSpeed = 8.0f;                                  // ノイズが先端へ流れる速さ（m/s）
    float noiseScale = 1.5f;                                   // ノイズの細かさ（1/m）
    float noiseStrength = 0.35f;                               // 幅の揺れ・明暗の強さ
    float tipFade = 1.0f;                                      // 先端を薄くする長さ（m）
    float rootFade = 0.3f;                                     // 根元を薄くする長さ（m）
    float growTime = 0.1f;                                     // 出る時、根元から先端へ伸びる秒数
    float shrinkTime = 0.2f;                                   // 消える時、根元から縮む秒数（duration の末尾）
    float previewLength = 12.0f;                               // 終点が入らない時の長さ（エディタ）
    bool  toon = false;                                        // トゥーン：断面を縁のはっきりした段に（json "toon"、2026-10-04）
    DirectX::SimpleMath::Vector3 previewDir = { 0, 0, 1 };

private:
    float m_Age = 0.0f;
    float m_Seed = 0.0f;
};
