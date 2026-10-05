// ============================================================
// InteractionSystem.h
// InteractableComponent を持つ物の面倒を見る:
//   - 浮遊と回転（TransformComponent を書き換える）
//   - プレイヤーに一番近い「使える」物を focus にする（画面の案内に使う）
//   - 押されたら、その Entity を返す
//   - 目印の点光源を PointLightManager へ積む
//
// 効果の適用と消去はシーンの仕事（ここは何が起きるかを知らない）。
// 例: RewardChoice → LevelUpSystem::OfferChoices、成功したら Destroy
// ============================================================
#pragma once
#include "ECS/Entity.h"

class Registry;

class InteractionSystem
{
public:
    // ゲームが止まっていないフレームだけ呼ぶ。
    // interactPressed: 今フレーム操作キーが押されたか（入力の判定は呼ぶ側）。
    // 押されて focus があれば、その Entity を返す。無ければ NULL_ENTITY
    Entity Update(Registry& reg, Entity player, float dt, bool interactPressed);

    // 点光源を積む。一時停止中も毎フレーム呼ぶ（表は毎フレーム空になるので、
    // 止まっている間に積まないと光が消える）。PointLightManager::Upload より前に
    void SubmitLights(Registry& reg);

    bool HasFocus() const { return m_Focus != EntityTraits::NULL_ENTITY; }
    Entity GetFocus() const { return m_Focus; }
    const wchar_t* GetPrompt() const { return m_Prompt; }

    // 使われた・消えた物を focus から外す（シーンが Destroy した直後に呼ぶ）
    void ClearFocus() { m_Focus = EntityTraits::NULL_ENTITY; m_Prompt = nullptr; }

    // ---- 見た目の調整値 ----
    float bobHeight = 0.12f;   // m（地面から浮く高さの幅。下端は地面）
    float bobSpeed = 2.0f;     // rad/s
    float spinSpeed = 45.0f;   // 度/s

private:
    Entity m_Focus = EntityTraits::NULL_ENTITY;
    const wchar_t* m_Prompt = nullptr;
    float m_Time = 0.0f;
};
