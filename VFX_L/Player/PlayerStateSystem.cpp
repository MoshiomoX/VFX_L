// ============================================================
// PlayerStateSystem.cpp
// ============================================================
#include "Player/PlayerStateSystem.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerTag.h"
#include "Component/HealthComponent.h"
#include "Component/WandComponent.h"
#include "ECS/View.h"
#include <cmath>

namespace
{
    // これより遅ければ静止とみなす（水平速度の二乗で比較する）
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
    // ※WeaponSystem が立てる castAnimTimer を見る（案B）。
    //   castTimer は使わない。あれは castInterval の残り時間で、
    //   撃っていない間もずっと減り続けるため、
    //   「今振っているか」の判定には使えない。
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
                // ---- 各層の滞在時間を進める ----
                st.moveTime += dt;
                st.actionTime += dt;
                st.damageTime += dt;

                if (st.invincibleTimer > 0.0f)
                    st.invincibleTimer -= dt;

                // ============================================================
                // 1) 被損層（最上位。他の層を抑制する）
                // ============================================================
                DamageStateID nextDamage = st.damage;

                if (hp.IsDead())
                {
                    nextDamage = DamageStateID::Dead;
                }
                else if (st.damage == DamageStateID::Hurt)
                {
                    // 硬直時間が過ぎたら通常へ戻す（無敵時間は別に続く）
                    if (st.damageTime >= st.hurtDuration)
                        nextDamage = DamageStateID::Normal;
                }
                else if (st.damage == DamageStateID::Dead)
                {
                    // 死亡からの復帰はここでは扱わない（外部が書き戻す）
                    nextDamage = DamageStateID::Dead;
                }

                if (nextDamage != st.damage)
                {
                    st.prevDamage = st.damage;
                    st.damage = nextDamage;
                    st.damageTime = 0.0f;
                }

                // 死んだら水平速度を止める（滑り続けるのを防ぐ）
                // ※velocity.y は触らない（重力と着地は PhysicsSystem の担当）
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
// 被弾の通知
// 無敵判定はここに閉じ込める。HitEvent を消費する側はこれを呼ぶだけ。
//   判定を2ヶ所に書くと必ず片方だけ直されて食い違う。
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
    if (st.IsInvincible()) return false;   // 無敵中は弾く

    hp.current -= damage;
    if (hp.invincible && hp.current < 0.0f)
        hp.current = 0.0f;

    // ここでは Hurt にするだけ。IsDead() の判定は次の Update が行う。
    st.prevDamage = st.damage;
    st.damage = DamageStateID::Hurt;
    st.damageTime = 0.0f;
    st.invincibleTimer = st.invincibleAfterHit;

    return true;
}