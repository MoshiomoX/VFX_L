// ============================================================
// PlayerStateSystem.cpp
// ============================================================
#include "PlayerStateSystem.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "RigidbodyComponent.h"
#include "PlayerStateComponent.h"
#include "PlayerTag.h"
#include "HealthComponent.h"
#include "WandComponent.h"
#include "View.h"
#include <cmath>

namespace
{
    // 水平速度がこれ以下なら静止扱い（浮動小数の残りかすを無視する）
    constexpr float kIdleSpeedSq = 0.04f;   // 0.2 m/s

    // ============================================================
    // 移動層
    // ============================================================
    MoveStateID NextMoveState(const RigidbodyComponent& rb)
    {
        if (!rb.isGrounded)
            return (rb.velocity.y > 0.0f) ? MoveStateID::Jump : MoveStateID::Fall;

        const float hSq = rb.velocity.x * rb.velocity.x
            + rb.velocity.z * rb.velocity.z;
        return (hSq > kIdleSpeedSq) ? MoveStateID::Run : MoveStateID::Idle;
    }

    // ============================================================
    // 動作層
    // ※WeaponSystem が発射時に castAnimTimer を置く（方案B）。
    //   castTimer から推測しない理由：castInterval は背包の修飾で
    //   変わるため「撃った直後」の窓を計算できない。
    //   この作品は魔法の出現と動作が厳密に揃う必要がある。
    // ============================================================
    ActionStateID NextActionState(const WandComponent* wand)
    {
        if (!wand) return ActionStateID::None;
        return (wand->castAnimTimer > 0.0f) ? ActionStateID::Casting
            : ActionStateID::None;
    }
}

void PlayerStateSystem::Update(Registry& reg, float dt)
{
    reg.CreateView<RigidbodyComponent, HealthComponent,
        PlayerStateComponent, PlayerTag>()
        .Each([&](Entity e, RigidbodyComponent& rb, HealthComponent& hp,
            PlayerStateComponent& st, PlayerTag&)
            {
                // ---- 滞在時間を進める ----
                st.moveTime += dt;
                st.actionTime += dt;
                st.damageTime += dt;

                if (st.invincibleTimer > 0.0f)
                    st.invincibleTimer -= dt;

                // ============================================================
                // 1) 被損層（最上位。ここから決める）
                // ============================================================
                DamageStateID nextDamage = st.damage;

                if (hp.IsDead())
                {
                    nextDamage = DamageStateID::Dead;
                }
                else if (st.damage == DamageStateID::Hurt)
                {
                    // 硬直が明けたら通常へ（無敵はまだ続いていてよい）
                    if (st.damageTime >= st.hurtDuration)
                        nextDamage = DamageStateID::Normal;
                }
                else if (st.damage == DamageStateID::Dead)
                {
                    // 蘇生は外部の責任。ここからは自動で戻らない。
                    nextDamage = DamageStateID::Dead;
                }

                if (nextDamage != st.damage)
                {
                    st.prevDamage = st.damage;
                    st.damage = nextDamage;
                    st.damageTime = 0.0f;
                }

                // 死亡時は滑り続けないよう水平速度を殺す。
                // ※velocity.y は触らない（重力と着地は PhysicsSystem の担当）。
                if (st.IsDead())
                {
                    rb.velocity.x = 0.0f;
                    rb.velocity.z = 0.0f;
                }

                // ============================================================
                // 2) 動作層（Damage に抑制される）
                // ============================================================
                ActionStateID nextAction = ActionStateID::None;

                if (!st.IsSuppressed(Mask_Action))
                {
                    const WandComponent* wand =
                        reg.Has<WandComponent>(e) ? &reg.Get<WandComponent>(e) : nullptr;
                    nextAction = NextActionState(wand);
                }

                if (nextAction != st.action)
                {
                    st.prevAction = st.action;
                    st.action = nextAction;
                    st.actionTime = 0.0f;
                }

                // ============================================================
                // 3) 移動層（最下位）
                // ============================================================
                if (!st.IsSuppressed(Mask_Move))
                {
                    const MoveStateID nextMove = NextMoveState(rb);
                    if (nextMove != st.move)
                    {
                        st.prevMove = st.move;
                        st.move = nextMove;
                        st.moveTime = 0.0f;
                    }
                }
            });
}

// ============================================================
// 被弾処理
// ※無敵判定をここに集約する。HitEvent 側で無敵を見ないこと。
//   見る場所が2つあると必ず片方だけ直して壊れる。
// ============================================================
bool PlayerStateSystem::TryApplyHit(Registry& reg, unsigned int entity, float damage)
{
    const Entity e = (Entity)entity;
    if (!reg.IsValid(e)) return false;
    if (!reg.Has<PlayerStateComponent>(e)) return false;
    if (!reg.Has<HealthComponent>(e))      return false;

    auto& st = reg.Get<PlayerStateComponent>(e);
    auto& hp = reg.Get<HealthComponent>(e);

    if (st.IsDead())       return false;
    if (st.IsInvincible()) return false;   // 無敵中は素通り

    hp.current -= damage;
    if (hp.invincible && hp.current < 0.0f)
        hp.current = 0.0f;

    // 硬直と無敵を開始する。IsDead() は次フレームの Update が拾う。
    st.prevDamage = st.damage;
    st.damage = DamageStateID::Hurt;
    st.damageTime = 0.0f;
    st.invincibleTimer = st.invincibleAfterHit;

    return true;
}