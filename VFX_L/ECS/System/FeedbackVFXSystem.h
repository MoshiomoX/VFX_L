// ============================================================
// FeedbackVFXSystem.h
// 反応の特効：升級・報酬の箱を開けた・被弾（Assets/Data/VFXData/LevelUp / CrateOpen / Hurt.json）。
//   再生は場面の AreaVFXPlayer（CPU の VFXEffect。Sprite entry も出る）。
//   AreaVFXPlayer は止まっている間（三択・一時停止）は進まないので、升級と開箱の
//   特効は三択を選び終えて動き出してから見える。
//   ・升級：レベルが上がったフレームに足元へ（プレイヤーに付いて動く）
//   ・開箱：箱の置き場所へ（呼ぶ側が OnCrateOpened で知らせる）
//   ・被弾：HP が減った時に体の前へ（付いて動く）。囲まれると無敵時間ごとに出て
//     うるさいので m_HurtInterval 秒はあける
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include <SimpleMath.h>
#include <functional>

class AreaVFXPlayer;
struct VFXContext;

class FeedbackVFXSystem
{
public:
    // player / ctx は場面の物（寿命は場面と同じ）
    void Init(AreaVFXPlayer* player, const VFXContext* ctx);

    // 升級と被弾の判定。hpLost = このフレームに減った HP。LevelUpSystem の後に呼ぶ
    void Update(Registry& reg, Entity player, float dt, float hpLost);
    void OnCrateOpened(const DirectX::SimpleMath::Vector3& pos);

    // 1 回出す。follow = プレイヤーに付いて動く（AreaVFXPlayer::Update の followPos）
    void Play(const char* file, const DirectX::SimpleMath::Vector3& pos, float duration, bool follow);

    void DrawImGui(Registry& reg, Entity player);

    // 出した時に呼ばれる（TEMP-TEST の自測ログ用）。空なら何もしない
    std::function<void(const char*)> onPlayed;

private:
    AreaVFXPlayer*    m_Player = nullptr;
    const VFXContext* m_Ctx = nullptr;

    bool  m_LevelUp = true;   // 升級：足元の光（LevelUp.json）
    bool  m_Crate = true;     // 報酬の箱を開けた（CrateOpen.json）
    bool  m_Hurt = true;      // 被弾：赤い斬撃（Hurt.json）
    int   m_PrevLevel = -1;   // -1 = まだ読んでいない
    float m_HurtInterval = 1.5f;
    float m_HurtTimer = 0.0f;
};
