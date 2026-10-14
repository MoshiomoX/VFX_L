// ============================================================
// PlayerVFXSystem.h
// プレイヤー自身の動きのエフェクト（2026-10-09 ユーザー：「プレイヤーのエフェクトを足したい、手応えが今ひとつなのはここかも」。
// 選んだ物：詠唱の手元の光・走る / 跳ぶ / 着地・スライディング・被弾の白い閃き + 「速さが一定を超えたら尾を引く」）。
//   ・詠唱の光：撃った弾ごとに杖口で閃光 + 撃つ向きへ火花（魔法の色に染める。CastFlash.json）。
//     WeaponSystem::GetCastFx（今フレームに撃った弾）を読む。同じ魔法は kCastMinGap 秒に 1 回まで
//   ・走る：地面にいて歩く速さ以上の時、足元に小さな土煙（FootDust.json）。左右の足を交互に、速いほど間が詰まる
//   ・跳ぶ：地面を離れた瞬間に足元へ土煙の輪（JumpDust.json）、空中ジャンプは魔法の光の輪（AirJumpRing.json）
//   ・着地：一定以上の時間空中にいたら土煙の輪（LandDust.json）、高い所から / 速く落ちたら大きな輪 + 土の粒（LandDustHeavy.json）
//   ・スライディング：滑っている間、足元から後ろへ土煙と草の切れ端（SlideDust.json）。速いほど細かく出す
//   ・速さの尾：水平の速さが trailSpeedOn を超えたら背中から大きな光の帯 1 本（SpeedTrail.json の Trail entry）、trailSpeedOff を下回ったら離す。
//     帯の根元は毎フレーム背骨（上背）の骨の位置 = モデルに付いて動く（同日ユーザー：「手や決まった所に付けない、背中に大きいの 1 本、モデルに付いて」）
//   ・被弾の閃き：HP が減ったら全身が白く、シールドで受けたら水色に一瞬光る（SkinnedAnimComponent::hitFlash → Dissolve.hlsli）
//   ・詠唱（同日の 2 回目）：手元の光（CastFlash.json を大きく）+ 足元の魔法陣と手の前の小さな魔法陣（CastCircles）+
//     体の周りに魔法の色の光の粒が昇る（CastPulse.json）と、体がほんのり魔法の色に光る（hitFlash を弱く使う）
// 足元の土煙は小石に替えた（同日ユーザー：「灰色の霧ではなく、巻き上がる小石」。KayKit の岩の Mesh 粒子）。
// 小石の色は面で変える（dustTint。シーンが Init の後に入れる）。
// 再生はシーンの AreaVFXPlayer（FeedbackVFXSystem と同じ）。魔法陣は SubmitMeshes で VFXMeshRenderer へ
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/System/WeaponSystem.h"
#include "ECS/System/CastCircles.h"
#include "VFX_Editor/VFXId.h"
#include <SimpleMath.h>
#include <cstdint>
#include <vector>

class AreaVFXPlayer;
class VFXMeshRenderer;
struct VFXContext;

class PlayerVFXSystem
{
public:
    void Init(AreaVFXPlayer* player, const VFXContext* ctx);
    // gameplay の毎フレーム（WeaponSystem の後）。hpLost = このフレームに減った HP
    void Update(Registry& reg, Entity player, float dt, float hpLost,
        const std::vector<WeaponSystem::CastFx>& casts);
    // シーンを出る時（速さの尾を離す）
    void Shutdown();
    // 魔法陣を積む（描画の直前、VFXMeshRenderer::Render の前）
    void SubmitMeshes(VFXMeshRenderer& renderer) { if (enabled && castCircles) m_Circles.Submit(renderer); }
    void DrawImGui();

    // ---- 調整 ----
    bool  enabled = true;
    bool  castFlash = true;
    bool  footDust = true;
    bool  jumpLand = true;
    bool  slideDust = true;
    bool  speedTrail = true;
    bool  hitFlash = true;
    bool  castCircles = true;      // 足元・手の前の魔法陣
    bool  castPulse = true;        // 体の周りの光の粒 + 体のほのかな光
    float pulseGap = 0.2f;         // 体の光の粒の最短間隔（秒）
    float castGlowStrength = 0.07f;   // 撃った時に体を魔法の色へ寄せる割合（被弾の閃きより弱く。0.12 だと撃ち続けで体が紫のままに見えた）
    float castGlowTime = 0.18f;
    float castMinGap = 0.06f;      // 同じ魔法の詠唱の光の最短間隔（秒。分裂・加速で 1 フレームに何発も出る）
    int   castMaxPerFrame = 4;
    float castTintGain = 1.5f;     // 魔法の色（一番明るい成分を 1 に揃えて）に掛ける（> 1 で bloom に乗る）
    float footMinSpeed = 1.5f;     // m/s。これより遅いと土煙を出さない
    float footInterval = 0.30f;    // 速さ footRefSpeed の時の一歩（秒）。速いほど詰まる
    float footRefSpeed = 5.0f;
    float landMinAir = 0.25f;      // 秒。これより短い空中は着地の土煙を出さない（段差の上り下り）
    float landHeavySpeed = 11.0f;  // m/s。落ちる速さがこれ以上なら大きな着地
    float slideIntervalSlow = 0.09f;   // スライディングの土煙の間隔（遅い時 / 速い時）
    float slideIntervalFast = 0.035f;
    float trailSpeedOn = 9.0f;     // m/s。水平の速さがこれを超えたら尾を引く（走りは 5、滑りは最大 20 前後）
    float trailSpeedOff = 7.5f;
    float hitFlashTime = 0.14f;    // 秒（閃きが消えるまで）
    float hitFlashStrength = 0.85f;
    DirectX::SimpleMath::Vector3 hurtFlashColor = { 1.5f, 1.45f, 1.4f };    // 線形。少し bloom に乗る白
    DirectX::SimpleMath::Vector3 shieldFlashColor = { 0.8f, 1.25f, 1.6f };  // シールドで受けた時
    DirectX::SimpleMath::Vector3 dustTint = { 1.0f, 1.0f, 1.0f };           // 土煙の色に掛ける（面ごと）

private:
    uint32_t Play(VFXId id, const DirectX::SimpleMath::Vector3& pos, float yawDeg, float duration,
        const DirectX::SimpleMath::Vector3& tint);
    void UpdateCasts(const std::vector<WeaponSystem::CastFx>& casts, const DirectX::SimpleMath::Vector3& center);
    void UpdateTrail(bool on, const DirectX::SimpleMath::Vector3& anchor);
    // 尾の根元：上背の骨の世界の位置（骨付きモデルが無ければカプセルの中心）
    DirectX::SimpleMath::Vector3 BackAnchor(Registry& reg, Entity player, const DirectX::SimpleMath::Vector3& center);

    AreaVFXPlayer*    m_Player = nullptr;
    const VFXContext* m_Ctx = nullptr;

    bool  m_Read = false;            // 前のフレームの値を読んだか（最初のフレームは比べない）
    bool  m_PrevGrounded = true;
    float m_AirTime = 0.0f;
    float m_MinVy = 0.0f;            // 空中で一番速く落ちた速さ（負）
    int   m_PrevAirJumps = 0;
    float m_FootTimer = 0.0f;
    int   m_FootSide = 0;
    float m_SlideTimer = 0.0f;
    uint32_t m_PrevShieldHits = 0;
    float m_Flash = 0.0f;            // 0..1（被弾の閃きの残り）
    DirectX::SimpleMath::Vector3 m_FlashColor = { 1.0f, 1.0f, 1.0f };

    // 同じ魔法の詠唱の光の間引き（ItemID 毎の最後の時刻）
    struct CastGap { int id; float time; };
    std::vector<CastGap> m_CastGaps;
    float m_Clock = 0.0f;

    bool     m_TrailOn = false;
    uint32_t m_Trail = 0;            // 背中の帯（AreaVFXPlayer のインスタンス）
    int      m_BackBone = -2;        // 上背の骨の番号（-2 = まだ探していない、-1 = 無い）

    CastCircles m_Circles;           // 足元・手の前の魔法陣
    float m_LastPulse = -1.0e9f;
    float m_CastGlow = 0.0f;         // 0..1（撃った時の体の光の残り）
    DirectX::SimpleMath::Vector3 m_CastGlowColor = { 1.0f, 1.0f, 1.0f };
};
