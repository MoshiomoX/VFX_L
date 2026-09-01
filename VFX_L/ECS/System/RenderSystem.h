// ============================================================
// RenderSystem.h
// ECS モデル描画 System
// TransformComponent + ModelComponent を持つ Entity を、
// 既存の Model::Draw（Renderer 経由）で描画する。
// 描画管線（Renderer / Mesh）は一切変更しない。
// ============================================================
#pragma once

class Registry;
class Renderer;

class RenderSystem
{
public:
    void Render(Registry& reg, Renderer& renderer);
};