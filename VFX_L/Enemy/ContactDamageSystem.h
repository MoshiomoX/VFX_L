// ============================================================
// ContactDamageSystem.h
// 敵に触れている間ダメージを受ける。
//
// CollisionSystem を通さない理由:
//   玩家は1体しか居ないので、敵ごとに XZ 距離を1回比べれば済む。
//   collider の対を作って形状判定まで回すのは、この用途には重すぎる。
//   （ExpOrbSystem が吸引を距離だけで済ませているのと同じ判断）
//
// 無敵時間の判定は PlayerStateSystem::TryApplyHit に任せる。
//   被弾の窓口を1つに保つため、ここでは HP を直接触らない。
// ============================================================
#pragma once

class Registry;

class ContactDamageSystem
{
public:
    void Update(Registry& reg, float dt);

    // --- 調整（ImGui から触る）---
    float damage = 8.0f;    // 1回の接触ダメージ
    float reach = 0.4f;    // 敵の見た目半径ぶんの上乗せ

    int GetLastTouchCount() const { return m_LastTouchCount; }

private:
    int m_LastTouchCount = 0;
};