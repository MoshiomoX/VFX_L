// ============================================================
// ExpOrbSystem.h
// 経験値オーブの吸い寄せと取得。
//
// 呼ぶ位置は投射物の後、カメラの前。
// プレイヤーの位置が確定した後に吸い寄せる必要があるため。
// ============================================================
#pragma once
#include "Entity.h"
#include <SimpleMath.h>
class Registry;

class ExpOrbSystem
{
public:
    void Update(Registry& reg, float dt);


    // 敵の死亡位置にオーブを生成する
    static Entity Spawn(Registry& reg, const DirectX::SimpleMath::Vector3& pos,
        float amount);

    // 倒された敵が持つ報酬に従って落とす。
    // ExpRewardComponent を持たない相手なら何もしない。
    static void DropFrom(Registry& reg, Entity deadEnemy);
    // ---- 調整値 ----
    float attractRadius = 4.0f;    // ここに入ると吸い寄せが始まる
    float pickupRadius = 0.6f;     // ここまで来たら取得
    float accel = 30.0f;           // 吸い寄せの加速度
    float maxSpeed = 18.0f;
    float gravity = -12.0f;        // 湧いた直後の跳ね用
    float orbSize = 0.25f;

    // ---- 統計（ImGui 表示用）----
    int GetOrbCount() const { return m_OrbCount; }
    float GetGainedThisFrame() const { return m_GainedThisFrame; }

private:
    int   m_OrbCount = 0;
    float m_GainedThisFrame = 0.0f;
};