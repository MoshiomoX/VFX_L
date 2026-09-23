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

class SkinnedAnimSystem
{
public:
    void Update(Registry& reg, float dt);

    // 3 層を混ぜて各骨の global 行列を作る（offset は掛けない）。
    // 描画側から毎フレーム呼ぶ。model が無ければ false
    static bool BuildPose(const SkinnedAnimComponent& anim,
        std::vector<DirectX::SimpleMath::Matrix>& outGlobal);
};
