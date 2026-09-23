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
#include "Graphics/Model/SkinnedModel.h"

namespace
{
    constexpr float kMoveFade = 0.15f;   // Idle ↔ Run
    constexpr float kCastFade = 0.08f;   // 施法の乗せ降ろし（遅いと撃ち始めが見えない）
    constexpr float kHurtFade = 0.05f;
    constexpr float kDeadFade = 0.10f;
}

void PlayerAnimSystem::Update(Registry& reg, float)
{
    reg.CreateView<PlayerStateComponent, SkinnedAnimComponent, PlayerTag>()
        .Each([&](Entity e, PlayerStateComponent& st, SkinnedAnimComponent& a, PlayerTag&)
            {
                if (!a.model) return;
                const SkinnedModel& model = *a.model;

                // 上半身マスクは初回に作る（モデルが決まってからでないと骨が分からない）
                if (a.upperMask.empty())
                    model.BuildBoneMask(m_Names.upperRoot, a.upperMask);

                // ============================================================
                // base: 移動層
                // ============================================================
                {
                    const std::string* name = &m_Names.idle;
                    switch (st.move)
                    {
                    case MoveStateID::Run:  name = &m_Names.run;  break;
                    case MoveStateID::Jump: name = &m_Names.jump; break;
                    case MoveStateID::Fall: name = &m_Names.fall; break;
                    default: break;
                    }
                    const int clip = model.FindClip(*name);
                    if (clip >= 0) a.base.Play(clip, true, 1.0f, kMoveFade);
                }

                // ============================================================
                // upper: 動作層（施法）
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
