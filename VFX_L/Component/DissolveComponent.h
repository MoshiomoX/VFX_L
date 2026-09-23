// ============================================================
// DissolveComponent.h
// モデルの溶解（燃焼消滅）状態。純データ。
//   progress 0 → 1 を duration 秒で進める（MeshVFXSystem::Update）。
//   RenderSystem が閾値 = progress で PS の溶解を掛け、
//   MeshVFXSystem が同じ閾値を EdgeFilterCS に渡して縁から粒子を出す。
//   progress が 1 に達したら destroyWhenDone なら実体ごと消す
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>

class Texture;

struct DissolveComponent
{
    std::shared_ptr<Texture> noise;                  // 無ければ溶解しない（PS も EdgeFilter も）
    DirectX::SimpleMath::Vector2 tiling = { 2.0f, 2.0f };
    DirectX::SimpleMath::Vector2 scroll = { 0.0f, 0.0f };

    float progress = 0.0f;                           // 0 = 無傷, 1 = 消滅
    float duration = 1.5f;                           // 秒
    float edge = 0.08f;                              // 燃える縁の幅（noise 値）
    DirectX::SimpleMath::Vector4 edgeColor = { 1.0f, 0.45f, 0.1f, 3.0f };   // a = 強さ（HDR、bloom に乗る）

    bool destroyWhenDone = true;

    float Threshold() const { return progress; }
    bool  IsDone() const { return progress >= 1.0f; }
};
