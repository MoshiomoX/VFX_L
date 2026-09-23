#pragma once
#include "VFX_Editor/VFXEntry.h"
#include "VFX_Editor/VFXTextureRef.h"
#include <memory>

class Model;
class VFXMeshRenderer;
struct EdgeFilterParams;

// ============================================================
// Mesh entry: モデルを特効として描く（剣気の弧、衝撃波の環、魔法陣）。
// 光は当てない。色 = 貼图 × tint × intensity（HDR）。
// 寿命中の変化は start → end の線形（曲線は後で）
// ============================================================
class VFXMeshEntry : public VFXEntry
{
public:
    EntryType GetType() const override { return EntryType::Mesh; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;
    std::unique_ptr<VFXEntry> Clone() const override;
    json ToJson() const override;
    void FromJson(const json& j) override;

    // VFXEffect::CollectAndDispatch から毎フレーム呼ばれる
    void Submit(VFXMeshRenderer& renderer, const DirectX::SimpleMath::Vector3& worldOffset);

    // 溶解の縁から粒子を出す用（EdgeFilterCS の入力）。
    // 今の閾値・noise・tiling・scroll を渡す。溶解 OFF か noise 無しなら false
    bool GetEdgeFilterParams(EdgeFilterParams& out) const;

    // ---- 参照 ----
    std::string   modelPath;
    VFXTextureRef mainTex;
    VFXTextureRef noiseTex;
    VFXTextureRef maskTex;

    // ---- 変換（effect の worldOffset からの相対）----
    DirectX::SimpleMath::Vector3 offset = { 0, 0, 0 };
    DirectX::SimpleMath::Vector3 rotationDeg = { 0, 0, 0 };
    DirectX::SimpleMath::Vector3 rotSpeedDeg = { 0, 0, 0 };   // 度/秒
    DirectX::SimpleMath::Vector3 scaleStart = { 1, 1, 1 };
    DirectX::SimpleMath::Vector3 scaleEnd = { 1, 1, 1 };

    // ---- 見た目 ----
    DirectX::SimpleMath::Vector4 tint = { 1, 1, 1, 1 };
    float alphaStart = 1.0f;
    float alphaEnd = 0.0f;
    float intensity = 1.0f;                                     // HDR。>1 で bloom
    int   blend = 0;                                            // 0 additive / 1 alpha
    bool  twoSided = true;

    // ---- UV ----
    DirectX::SimpleMath::Vector2 mainTiling = { 1, 1 };
    DirectX::SimpleMath::Vector2 mainScroll = { 0, 0 };        // UV/秒
    DirectX::SimpleMath::Vector2 noiseTiling = { 1, 1 };
    DirectX::SimpleMath::Vector2 noiseScroll = { 0, 0 };
    float distortion = 0.0f;                                    // noise で main の UV を歪める

    // ---- 溶解 ----
    bool  dissolveEnabled = false;
    float dissolveStart = 0.0f;                                 // 閾値 start → end
    float dissolveEnd = 1.0f;
    float dissolveEdge = 0.05f;
    DirectX::SimpleMath::Vector4 dissolveEdgeColor = { 1, 0.5f, 0.1f, 1 };

private:
    void LoadModel();
    float Progress() const;   // 0 → 1（duration<0 なら 0 固定）

    std::shared_ptr<Model> m_Model;
    float m_Age = 0.0f;
};