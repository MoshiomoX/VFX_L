// ============================================================
// SkinnedAnimSystem.h
// SkinnedAnimComponent の時計を進める。
//   ループ / 末尾 clamp / クロスフェード / 層 weight の追従だけ。
//   「どのクリップを流すか」は決めない（それは PlayerAnimSystem）。
//   描画は RenderSystem（BuildPose をそこから呼ぶ）。
// ============================================================
#pragma once
#include <vector>
#include <SimpleMath.h>

class Registry;
struct SkinnedAnimComponent;
struct TransformComponent;

class SkinnedAnimSystem
{
public:
    void Update(Registry& reg, float dt);

    // 3 層を混ぜて各骨の global 行列を作る（offset は掛けない）。
    // 描画側から毎フレーム呼ぶ。model が無ければ false。
    // applyOverrides = false で boneOverrides（揺れ物の結果）を掛けない素のポーズ
    static bool BuildPose(const SkinnedAnimComponent& anim,
        std::vector<DirectX::SimpleMath::Matrix>& outGlobal, bool applyOverrides = true);
    // BuildPose の使い回しの回数（起動からの累計。自動テスト / 計測用、2026-10-23）
    static void PoseCacheStats(int& hits, int& misses);

    // モデル空間 → 世界: 拡縮 → 足元を軸に傾ける（滑り）→ offset → Entity の回転 → 位置
    static DirectX::SimpleMath::Matrix WorldMatrix(const TransformComponent& tf, const SkinnedAnimComponent& anim);
};
