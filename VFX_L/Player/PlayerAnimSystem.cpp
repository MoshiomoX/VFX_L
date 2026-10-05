// ============================================================
// PlayerAnimSystem.cpp
// ============================================================
#include "Player/PlayerAnimSystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerTag.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/WandComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Graphics/Model/SkinnedModel.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kMoveFade = 0.15f;   // Idle ↔ Run
    constexpr float kGaitFade = 0.25f;   // Walk ↔ Jog ↔ Sprint（位相を揃えるので長めでも崩れない）
    constexpr float kCastFade = 0.08f;   // 詠唱の乗せ降ろし（遅いと撃ち始めが見えない）
    constexpr float kHurtFade = 0.05f;
    constexpr float kDeadFade = 0.10f;
}

void PlayerAnimSystem::Update(Registry& reg, float dt)
{
    reg.CreateView<PlayerStateComponent, SkinnedAnimComponent, PlayerTag>()
        .Each([&](Entity e, PlayerStateComponent& st, SkinnedAnimComponent& a, PlayerTag&)
            {
                if (!a.model) return;
                const SkinnedModel& model = *a.model;

                // 上半身マスクは初回に作る（モデルが決まってからでないと骨が分からない）
                if (a.upperMask.empty() && !model.BuildBoneMask(m_Names.upperRoot, a.upperMask))
                    model.BuildBoneMask(m_Names.upperRootAlt, a.upperMask);

                // ============================================================
                // base: 移動層
                // ============================================================
                {
                    // 水平の速さ（歩様と再生速度に使う）。体は PlayerControlSystem が進む方へ向けている
                    float hSpeed = 0.0f;
                    if (reg.Has<RigidbodyComponent>(e))
                    {
                        const auto& rb = reg.Get<RigidbodyComponent>(e);
                        hSpeed = std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
                    }

                    const std::string* name = &m_Names.idle;
                    const bool gait = (st.move == MoveStateID::Run);
                    switch (st.move)
                    {
                    case MoveStateID::Run:
                    {
                        // 境目の前後 ±h で行き来を抑える（今その上にいるなら下がるのは th-h から）
                        const float h = gaitHysteresis * 0.5f;
                        auto above = [&](float th, bool nowAbove) { return hSpeed > th + (nowAbove ? -h : h); };
                        const bool jog = above(walkToJog, m_Gait >= 1);
                        const bool sprint = above(jogToSprint, m_Gait >= 2);
                        m_Gait = sprint ? 2 : (jog ? 1 : 0);
                        name = (m_Gait == 2) ? &m_Names.sprint : (m_Gait == 1) ? &m_Names.run : &m_Names.walk;
                        break;
                    }
                    case MoveStateID::Jump: name = &m_Names.jump; break;
                    case MoveStateID::Fall: name = &m_Names.fall; break;
                    case MoveStateID::Slide: name = &m_Names.slide; break;
                    default: break;
                    }
                    const int clip = model.FindClip(*name);
                    if (clip >= 0)
                    {
                        // 歩様どうしの切り替えは足の位相を引き継ぐ（左足接地 → 左足接地）
                        const int walkClip = model.FindClip(m_Names.walk);
                        const int jogClip = model.FindClip(m_Names.run);
                        const int sprintClip = model.FindClip(m_Names.sprint);
                        auto isGaitClip = [&](int c) { return c >= 0 && (c == walkClip || c == jogClip || c == sprintClip); };

                        const int prev = a.base.clip;
                        if (gait && prev != clip && isGaitClip(prev))
                        {
                            const float prevDur = model.GetClipDurationSec(prev);
                            const float phase = (prevDur > 0.0f) ? a.base.time / prevDur : 0.0f;
                            a.base.Play(clip, true, 1.0f, kGaitFade);
                            a.base.time = phase * model.GetClipDurationSec(clip);
                        }
                        else
                        {
                            a.base.Play(clip, true, 1.0f, kMoveFade);
                        }

                        // 再生速度 = 実速度 / クリップの素の速さ
                        m_PlayRate = 1.0f;
                        if (gait)
                        {
                            const float ref = (m_Gait == 2) ? sprintRefSpeed : (m_Gait == 1) ? jogRefSpeed : walkRefSpeed;
                            if (ref > 0.0f) m_PlayRate = std::clamp(hSpeed / ref, minPlayRate, maxPlayRate);
                        }
                        a.base.speed = m_PlayRate;
                    }

                    // 滑りの間は足元を軸に後ろへ傾ける（急に倒れないよう角度を寄せていく）
                    const float leanTarget = (st.move == MoveStateID::Slide) ? slideLeanDeg : 0.0f;
                    const float step = leanSpeedDeg * dt;
                    a.leanDeg += (std::max)(-step, (std::min)(step, leanTarget - a.leanDeg));

                }

                // ============================================================
                // upper: 動作層（詠唱）
                // 撃った瞬間に頭出し。連射中も 1 発ごとに振り直す
                // ============================================================
                {
                    const int castClip = model.FindClip(m_Names.cast);
                    a.upper.fadeDuration = kCastFade;

                    if (st.action == ActionStateID::Casting && castClip >= 0)
                    {
                        bool firedNow = false;
                        if (reg.Has<WandComponent>(e))
                        {
                            const auto& w = reg.Get<WandComponent>(e);
                            firedNow = (w.castAnimTimer >= w.castAnimDuration);
                        }
                        a.upper.Play(castClip, false, 1.0f, kCastFade, firedNow);
                        a.upper.targetWeight = 1.0f;
                    }
                    else if (a.upper.clip == castClip && !a.upper.finished && a.upper.targetWeight > 0.0f)
                    {
                        // castAnimTimer（0.3s）よりクリップの方が長い。
                        // 状態が None に戻っても振り終わるまでは乗せたままにする
                    }
                    else
                    {
                        a.upper.targetWeight = 0.0f;
                    }
                }

                // ============================================================
                // over: 被損層
                // Hurt は入った瞬間に頭出し。前フレームが Normal（targetWeight 0）なら
                // 入った瞬間と見なす（無敵時間 > 硬直時間なので Hurt→Hurt の連続は無い）。
                // Dead は 1 回流して末尾で止める（loop=false）
                // ============================================================
                switch (st.damage)
                {
                case DamageStateID::Hurt:
                {
                    const int clip = model.FindClip(m_Names.hurt);
                    const bool entered = (a.over.targetWeight <= 0.0f);
                    if (clip >= 0) a.over.Play(clip, false, 1.0f, kHurtFade, entered);
                    a.over.fadeDuration = kHurtFade;
                    a.over.targetWeight = 1.0f;
                    break;
                }
                case DamageStateID::Dead:
                {
                    const int clip = model.FindClip(m_Names.dead);
                    if (clip >= 0) a.over.Play(clip, false, 1.0f, kDeadFade);
                    a.over.fadeDuration = kDeadFade;
                    a.over.targetWeight = 1.0f;
                    break;
                }
                default:
                    a.over.fadeDuration = kHurtFade;
                    a.over.targetWeight = 0.0f;
                    break;
                }
            });
}
