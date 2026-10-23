// ============================================================
// ClothChainSystem.h
// ClothChainComponent（骨の鎖。マント）を Verlet で動かす（2026-10-04）。
// 2026-10-23 から横 columns 列の格子（列の骨は読み込み時に SkinnedModel::SplitClothChain が足す）。
//   毎フレーム（SkinnedAnimSystem::Update の後）：
//     1. 層を混ぜた素のポーズを作り、鎖を上背（anchorBone）に付けた形 = 目標の形を出す
//     2. 世界空間で 1/120 秒刻みに: 重力 + 空気抵抗（風との相対速度）+ はためき → 目標の形へ少し戻す →
//        縦 / 縦の 1 つ飛ばし / 横 / 斜め / 横の 1 つ飛ばしの距離 / 体のカプセル / 地面の拘束を数回
//     3. ノードをモデル空間へ戻し、各骨の global を「目標の形の向きを最小の回転で縦（下の段へ）に合わせ、
//        格子なら横（左右の隣）も合うよう捻った」物にして SkinnedAnimComponent::boneOverrides に書く
//        （描画の BuildPose が差し替える）。無効の時は上背に付いたままの形を書く
//   一時停止中は呼ばれないので止まる
// ============================================================
#pragma once
#include "World/WindState.h"
#include "World/LocalWind.h"
#include <functional>

class Registry;
struct ClothChainComponent;

class ClothChainSystem
{
public:
    void Update(Registry& reg, float dt);

    // 地面の高さ（世界の x, z → y）。無ければ足元（モデルの原点）の高さの平面
    std::function<float(float, float)> groundHeight;
    // 世界の風（シーンが毎フレーム WeatherSystem::Wind を入れる。2026-10-12）。windTime は波の時計（草と同じ物）
    WindState wind;
    float windTime = 0.0f;
    // 局所の風源（竜巻・渦・砂旋・突風。SwarmSystem::GetLocalWind、2026-10-22）。null = 無し。根の位置で引いて世界の風に足す
    const LocalWind* localWind = nullptr;

    // Reallusion CC 骨（Shadowkin）のマント: Cloak1〜8（+ 横 5 列）を CC_Base_Spine02 に付ける（BoneMaps.h の kCCCape*）
    static ClothChainComponent CCCape();

    // Player パネルの「Cape」
    static void DrawImGui(ClothChainComponent& c);
};
