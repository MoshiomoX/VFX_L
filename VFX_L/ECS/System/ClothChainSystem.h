// ============================================================
// ClothChainSystem.h
// ClothChainComponent（骨の 1 本鎖。マント）を Verlet で動かす（2026-10-04）。
//   毎フレーム（SkinnedAnimSystem::Update の後）：
//     1. 層を混ぜた素のポーズを作り、鎖を上背（anchorBone）に付けた形 = 目標の形を出す
//     2. 世界空間で 1/120 秒刻みに: 重力 + 空気抵抗（風との相対速度）→ 目標の形へ少し戻す →
//        隣の距離 / 1 つ飛ばしの距離 / 体のカプセル / 地面の拘束を数回
//     3. ノードをモデル空間へ戻し、各骨の global を「目標の形の向きを最小の回転でノードの向きへ」
//        回した物にして SkinnedAnimComponent::boneOverrides に書く（描画の BuildPose が差し替える）
//   一時停止中は呼ばれないので止まる
// ============================================================
#pragma once
#include <functional>

class Registry;
struct ClothChainComponent;

class ClothChainSystem
{
public:
    void Update(Registry& reg, float dt);

    // 地面の高さ（世界の x, z → y）。無ければ足元（モデルの原点）の高さの平面
    std::function<float(float, float)> groundHeight;

    // Reallusion CC 骨（Shadowkin）のマント: Cloak1〜8 を CC_Base_Spine02 に付ける
    static ClothChainComponent CCCape();

    // Player パネルの「Cape」
    static void DrawImGui(ClothChainComponent& c);
};
