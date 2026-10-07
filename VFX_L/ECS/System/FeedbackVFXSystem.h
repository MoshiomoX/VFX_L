// ============================================================
// FeedbackVFXSystem.h
// 反応のエフェクト：レベルアップ・報酬の箱を開けた・被弾・経験値を拾った
//（Assets/Data/VFXData/LevelUp / CrateOpen / Hurt / ExpPickup.json）。
//   再生はシーンの AreaVFXPlayer（CPU の VFXEffect。Sprite entry も出る）。
//   AreaVFXPlayer は止まっている間（三択・一時停止）は進まないので、レベルアップと開箱の
//   エフェクトは三択を選び終えて動き出してから見える。
//   ・レベルアップ：レベルが上がったフレームに足元へ（プレイヤーに付いて動く）
//   ・開箱：箱の置き場所へ（呼ぶ側が OnCrateOpened で知らせる）
//   ・被弾：HP が減った時に体の前へ（付いて動く）。囲まれると無敵時間ごとに出て
//     うるさいので m_HurtInterval 秒はあける
//   ・経験値：GPU で拾った分がリードバックで届いたフレームに体の周りへ（付いて動く）。
//     吸い寄せで何百個も一度に拾うので、m_PickupInterval 秒に 1 回まで
//   ・魔力解放（2026-10-01）：始まったフレームに金の爆発（ManaSurgeBurst.json）、
//     解放の間ずっと体の周りに金の光（ManaSurgeAura.json、loop。残り時間で止める）。
//     どちらも付いて動く。解放が早く終わったら光も止める
//   ・シールド（2026-10-07）：シールドで受けた時に六角の護罩（fanghuzhao2.FBX）が一瞬光る（付いて動く）。
//     シールドが 0 になった時は砕けた護罩（suipian01/02.FBX）が外へ広がって消える（その場に残す）
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/System/ShieldBubble.h"
#include <SimpleMath.h>
#include <cstdint>
#include <functional>

class AreaVFXPlayer;
class VFXMeshRenderer;
struct VFXContext;

class FeedbackVFXSystem
{
public:
    // player / ctx はシーンの物（寿命はシーンと同じ）
    void Init(AreaVFXPlayer* player, const VFXContext* ctx);

    // レベルアップと被弾の判定。hpLost = このフレームに減った HP。LevelUpSystem の後に呼ぶ
    void Update(Registry& reg, Entity player, float dt, float hpLost);
    void OnCrateOpened(const DirectX::SimpleMath::Vector3& pos);
    // 経験値を拾った（SwarmSystem::ConsumeExp が > 0 の時）。pos = プレイヤーの位置
    void OnExpPicked(const DirectX::SimpleMath::Vector3& pos);
    // シールドの護罩（常に包む物）を積む。描画の直前、VFXMeshRenderer::Render の前に呼ぶ
    void SubmitMeshes(VFXMeshRenderer& renderer, Registry& reg, Entity player);

    // 1 回出す。follow = プレイヤーに付いて動く（AreaVFXPlayer::Update の followPos）。戻り値はインスタンスの番号（0 = 出せなかった）
    uint32_t Play(const char* file, const DirectX::SimpleMath::Vector3& pos, float duration, bool follow);

    void DrawImGui(Registry& reg, Entity player);

    // 出した時に呼ばれる（TEMP-TEST の自動テストログ用）。空なら何もしない
    std::function<void(const char*)> onPlayed;

private:
    AreaVFXPlayer*    m_Player = nullptr;
    const VFXContext* m_Ctx = nullptr;

    bool  m_LevelUp = true;   // レベルアップ：足元の光（LevelUp.json）
    bool  m_Crate = true;     // 報酬の箱を開けた（CrateOpen.json）
    bool  m_Hurt = true;      // 被弾：赤い斬撃（Hurt.json）
    bool  m_ExpPickup = true; // 経験値を拾った：水色のきらめき（ExpPickup.json）
    int   m_PrevLevel = -1;   // -1 = まだ読んでいない
    float m_HurtInterval = 1.5f;
    float m_HurtTimer = 0.0f;
    float m_PickupInterval = 0.15f;
    float m_PickupTimer = 0.0f;

    bool     m_Surge = true;          // 魔力解放：金の爆発 + 体の周りの光
    float    m_PrevSurgeTime = 0.0f;  // 前のフレームの ManaComponent::surgeTime（増えたら始まった）
    uint32_t m_SurgeAura = 0;         // 光のインスタンス（0 = 出していない）

    // シールド（2026-10-07）：受けた = 六角の護罩が光る（ShieldHit.json）、割れた = 護罩が砕ける（ShieldBreak.json）
    bool     m_Shield = true;
    bool     m_ShieldRead = false;    // 前のフレームの累計を読んだか（最初のフレームは比べない）
    uint32_t m_PrevShieldHits = 0;
    uint32_t m_PrevShieldBreaks = 0;
    ShieldBubble m_Bubble;            // シールドが残っている間、体を包む薄い六角の護罩（受けた瞬間は赤）
};
