// ============================================================
// RenderSystem.h
// ECS モデル描画 System
//   TransformComponent + ModelComponent      … 既存の Model::Draw（Renderer 経由）
//   TransformComponent + SkinnedAnimComponent … 層ブレンド → SkinningCS → SkinnedModelGPU
// 描画パイプライン（Renderer / Mesh）は一切変更しない。
// ============================================================
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

class Registry;
class Renderer;
class ComputeShader;

class RenderSystem
{
public:
    void Render(Registry& reg, Renderer& renderer);

    // シャドウマップへ深度だけ（renderer が BeginDepthPass 中）。影を落とす物 = 見えている実体全部。
    // skin = true で先に骨付きのスキニングをやり直す（カスケードの最初の段だけ true にすればよい）
    void RenderDepth(Registry& reg, Renderer& renderer, bool skin);

private:
    void RenderSkinned(Registry& reg, Renderer& renderer);
    bool EnsureSkinningCS();
    // 描く実体（見えていてモデルがあり、StaticPropRenderer に任せていない物）を集める（2026-10-03）。
    // ModelComponent の pool は装飾物（batched、1100 個ほど）も含むので、影の 3 段 + 本描画の 4 回それぞれで
    // 全部を回していた（Debug で約 1 ms）。1 フレームに 1 回集め、RenderDepth と Render で使い回す
    void GatherDrawables(Registry& reg);

    std::shared_ptr<ComputeShader> m_SkinningCS;   // 初回描画時に ResourceManager から取る
    std::vector<uint32_t> m_Drawables;              // Entity
    bool m_DrawablesFresh = false;                  // このフレームで集めた（Render の最後で戻す）
};
