// ============================================================
// RenderSystem.h
// ECS モデル描画 System
//   TransformComponent + ModelComponent      … 既存の Model::Draw（Renderer 経由）
//   TransformComponent + SkinnedAnimComponent … 層ブレンド → SkinningCS → SkinnedModelGPU
// 描画管線（Renderer / Mesh）は一切変更しない。
// ============================================================
#pragma once
#include <memory>

class Registry;
class Renderer;
class ComputeShader;

class RenderSystem
{
public:
    void Render(Registry& reg, Renderer& renderer);

private:
    void RenderSkinned(Registry& reg, Renderer& renderer);

    std::shared_ptr<ComputeShader> m_SkinningCS;   // 初回描画時に ResourceManager から取る
};
