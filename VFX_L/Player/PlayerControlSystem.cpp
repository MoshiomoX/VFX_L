// ============================================================
// PlayerControlSystem.cpp
// ============================================================
#include "Player/PlayerControlSystem.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Player/PlayerTag.h"
#include "Manager/InputMap.h"
#include "ECS/View.h"
#include "Component/WandComponent.h"
#include "Component/ManaComponent.h"
#include "Debug/DebugManager.h"
#include "ImGui.h"
#include <algorithm>
#include <cmath>

namespace
{
    // 滑り中に地面から離れても、この秒数までは滑りを続ける（坂の継ぎ目・小さな段差で途切れない）
    constexpr float kSlideAirGrace = 0.2f;

    // 水平ベクトル from の向きを toDir へ最大 maxRad だけ回す（長さは from のまま）
    Vector3 TurnToward(const Vector3& from, const Vector3& toDir, float maxRad)
    {
        const float len = from.Length();
        if (len < 1e-4f || toDir.LengthSquared() < 1e-6f) return from;
        const float a0 = std::atan2(from.x, from.z);
        const float a1 = std::atan2(toDir.x, toDir.z);
        float d = a1 - a0;
        while (d > DirectX::XM_PI) d -= DirectX::XM_2PI;
        while (d < -DirectX::XM_PI) d += DirectX::XM_2PI;
        d = (std::max)(-maxRad, (std::min)(maxRad, d));
        const float a = a0 + d;
        return Vector3(std::sin(a) * len, 0.0f, std::cos(a) * len);
    }

    Vector3 WithLength(Vector3 v, float len)
    {
        if (v.LengthSquared() < 1e-8f) return Vector3::Zero;
        v.Normalize();
        return v * len;
    }
}

void PlayerControlSystem::ApplyKnockback(Registry& reg, unsigned int entity, Vector2 dir, bool blast)
{
    const Entity e = (Entity)entity;
    if (!reg.IsValid(e) || !reg.Has<PlayerStateComponent>(e) || !reg.Has<PlayerStatsComponent>(e)) return;
    auto& st = reg.Get<PlayerStateComponent>(e);
    const auto& stats = reg.Get<PlayerStatsComponent>(e);
    if (st.IsDead() || dir.LengthSquared() < 1e-6f) return;
    dir.Normalize();

    // 止まっている時に dist だけ下がる初速（直線的に 0 へ減るので 平均 = 初速 / 2）
    const float bodies = blast ? stats.knockBlastBodies : stats.knockMeleeBodies;
    const float duration = (std::max)(0.01f, blast ? stats.knockBlastTime : stats.knockMeleeTime);
    const float dist = bodies * stats.radius * 2.0f;
    const float v0 = 2.0f * dist / duration;

    // 爆発が近接より強い。弱い方が強い方を上書きしない（爆発の直後に殴られても飛ばされ続ける）
    const float remaining = (st.knockDuration > 0.0f)
        ? std::sqrt(st.knockX * st.knockX + st.knockZ * st.knockZ) * st.knockTime / st.knockDuration : 0.0f;
    if (remaining > v0) return;

    st.knockX = dir.x * v0;
    st.knockZ = dir.y * v0;
    st.knockTime = st.knockDuration = duration;

    if (blast && reg.Has<RigidbodyComponent>(e))
    {
        auto& rb = reg.Get<RigidbodyComponent>(e);
        if (rb.velocity.y < stats.knockBlastLift)
        {
            rb.velocity.y = stats.knockBlastLift;
            rb.isGrounded = false;
        }
    }
}

void PlayerControlSystem::Update(Registry& reg, float dt, CameraBase* camera)
{
    const bool blocked = !testInput &&
        (DebugManager::Get().IsUsingDebugCamera() || ImGui::GetIO().WantCaptureKeyboard);

    const bool castTrigger = blocked ? false : InputMap::GetCastTrigger();
    const bool surgeTrigger = testInput ? testSurge : (blocked ? false : InputMap::GetManaSurgeTrigger());
    testSurge = false;

    reg.CreateView<WandComponent, PlayerTag>()
        .Each([&](Entity e, WandComponent& wand, PlayerTag&)
            {

                wand.castRequested = castTrigger;
            });

    // 魔力解放（3 秒間魔力を消費しない、30 秒に 1 回。再使用待ちの間は何も起きない）。
    // 施法の一時停止はプレイヤーの操作から外した（2026-10-01。WandComponent::castingPaused は調試面板・自測用に残る）
    if (surgeTrigger)
        reg.CreateView<ManaComponent, PlayerTag>()
            .Each([&](Entity, ManaComponent& mana, PlayerTag&) { mana.TryStartSurge(); });

    if (blocked) return;
    if (!camera) return;

    const Vector2 move = testInput ? testMove : InputMap::GetMoveInput();
    const bool slideHeld = testInput ? testSlide : InputMap::GetSlideHeld();
    const bool jumpPressed = testInput ? testJump : InputMap::GetJumpTrigger();

    Vector3 camF = camera->GetForward(); camF.y = 0; camF.Normalize();
    Vector3 camR = camera->GetRight();   camR.y = 0; camR.Normalize();

    Vector3 dir = camR * move.x + camF * move.y;
    reg.CreateView<TransformComponent, RigidbodyComponent,
        PlayerStatsComponent, PlayerStateComponent, PlayerTag>()
        .Each([&](Entity e, TransformComponent& tf, RigidbodyComponent& rb,
            PlayerStatsComponent& stats, PlayerStateComponent& st, PlayerTag&)
            {
                // 能力値は Entity が持つ。System は値を持たない
                // 前フレームに足したノックバックは引いて戻す（滑り・勢いの持ち越しに混ざらないように）
                Vector3 hv(rb.velocity.x - st.knockAppliedX, 0.0f, rb.velocity.z - st.knockAppliedZ);
                st.knockAppliedX = st.knockAppliedZ = 0.0f;
                const bool grounded = rb.isGrounded;
                const bool canMove = !st.IsSuppressed(Mask_Move);
                if (st.slideBoostTimer > 0.0f) st.slideBoostTimer -= dt;

                // ---- 滑りの出入り ----
                if (!slideHeld) st.slideNeedsRelease = false;
                if (st.slideActive)
                {
                    st.slideAirTime = grounded ? 0.0f : st.slideAirTime + dt;
                    if (!slideHeld || !canMove || st.slideAirTime > kSlideAirGrace)
                    {
                        st.slideActive = false;
                        st.carryMomentum = true;   // 浮いた・離した: 勢いは持ち越す
                    }
                }
                else if (slideHeld && grounded && canMove && !st.slideNeedsRelease)
                {
                    st.slideActive = true;
                    st.slideAirTime = 0.0f;
                    // 向き: 今動いている方 → 入力の方 → カメラの前
                    Vector3 d = (hv.LengthSquared() > 0.01f) ? hv
                        : ((dir.LengthSquared() > 1e-4f) ? dir : camF);
                    float sp = hv.Length();
                    // 平地でも入った瞬間に一押し。再使用待ちの間は今の速さのまま（連打で加速しない）
                    if (st.slideBoostTimer <= 0.0f)
                    {
                        sp = (std::max)(sp, stats.moveSpeed * stats.slideBoost);
                        st.slideBoostTimer = stats.slideBoostCooldown;
                    }
                    hv = WithLength(d, sp);
                }

                if (st.slideActive)
                {
                    // 坂: 重力を足元の面に射影した物の水平成分 = n.y * (n.x, n.z)（大きさ cosθ sinθ）
                    if (grounded)
                    {
                        const Vector3& n = rb.groundNormal;
                        hv += Vector3(n.x, 0.0f, n.z) * (n.y * stats.slideGravity * dt);
                    }
                    float sp = hv.Length();
                    sp = (std::max)(0.0f, sp - stats.slideFriction * dt);
                    sp = (std::min)(sp, stats.slideMaxSpeed);
                    hv = WithLength(TurnToward(hv, dir, DirectX::XMConvertToRadians(stats.slideTurnRate) * dt), sp);
                    // 止まりかけたら立つ。押しっぱなしのまま滑り直す（押し出しを繰り返す）のは離すまで待つ
                    if (grounded && sp < stats.slideMinSpeed)
                    {
                        st.slideActive = false;
                        st.slideNeedsRelease = true;
                    }
                }
                else
                {
                    const float sp = hv.Length();
                    // 滑りの勢いは持ち越す。普段の走りは入力のまま（坂で物理が足した分も持ち越さない）
                    if (st.carryMomentum && sp > stats.moveSpeed + 0.05f)
                    {
                        // 滑りの勢いが残っている: 入力方向へ舵を切りつつ、moveSpeed を超えた分を減らす
                        const float decay = grounded ? stats.momentumDecay : stats.momentumDecayAir;
                        hv = WithLength(TurnToward(hv, dir, DirectX::XMConvertToRadians(stats.momentumTurnRate) * dt),
                            (std::max)(stats.moveSpeed, sp - decay * dt));
                    }
                    else
                    {
                        hv = dir * stats.moveSpeed;   // 普段: 入力にそのまま従う
                        st.carryMomentum = false;
                    }
                }

                // ---- ノックバック: 初速から直線的に 0 へ。普段の走りの間は入力の効きを 0 → 1 へ戻す ----
                // （敵へ向かって走っていても下がる。滑り・勢いの持ち越しはそのまま、上に足すだけ）
                Vector3 knock = Vector3::Zero;
                if (st.knockTime > 0.0f && st.knockDuration > 0.0f)
                {
                    const float f = st.knockTime / st.knockDuration;   // 1 → 0
                    knock = Vector3(st.knockX, 0.0f, st.knockZ) * f;
                    if (!st.slideActive && !st.carryMomentum) hv *= (1.0f - f);
                    st.knockTime = (std::max)(0.0f, st.knockTime - dt);
                }
                st.knockAppliedX = knock.x;
                st.knockAppliedZ = knock.z;

                rb.velocity.x = hv.x + knock.x;
                rb.velocity.z = hv.z + knock.z;

                // 向き: カメラの前から見て 前 / 右 / 後 / 左 のどちらへ進んでいるかを出し、体をその方向へ振り向かせる
                // （後ろへ走ればカメラの方を向いて走る）。止まっている間は最後の向きのまま。
                // 滑っている間はこれまで通り進む方へすぐ向ける
                if (hv.LengthSquared() > 0.01f)
                {
                    const float camYaw = DirectX::XMConvertToDegrees(std::atan2(camF.x, camF.z));
                    const float moveYaw = DirectX::XMConvertToDegrees(std::atan2(hv.x, hv.z));
                    st.moveAngleCam = std::remainder(camYaw - moveYaw, 360.0f);   // +90 = カメラの右
                    const float a = st.moveAngleCam;
                    st.moveDir = (std::fabs(a) <= 45.0f) ? MoveDirID::Forward
                        : (std::fabs(a) >= 135.0f) ? MoveDirID::Back
                        : (a > 0.0f) ? MoveDirID::Right : MoveDirID::Left;

                    if (st.slideActive)
                        tf.rotation.y = moveYaw;
                    else
                    {
                        const float step = stats.faceTurnRate * dt;
                        const float d = std::remainder(moveYaw - tf.rotation.y, 360.0f);
                        tf.rotation.y = std::remainder(tf.rotation.y + std::clamp(d, -step, step), 360.0f);
                    }
                }
                else
                    st.moveDir = MoveDirID::None;

                if (grounded) st.airJumpsUsed = 0;

                if (grounded && jumpPressed)
                {
                    rb.velocity.y = stats.jumpPower;
                    rb.isGrounded = false;
                    if (st.slideActive) st.carryMomentum = true;   // 水平の勢いは持ったまま空中へ
                    st.slideActive = false;
                }
                else if (!grounded && jumpPressed && st.airJumpsUsed < stats.extraJumps)
                {
                    // 空中の追加ジャンプ: 回を重ねるごとに弱くなる（airJumpFalloff 0.75 なら 12 → 9 → 6.75 …）。
                    // 落ちている最中でも上向きの速さを置き換える（足すと落下の勢いに負ける）
                    ++st.airJumpsUsed;
                    rb.velocity.y = stats.jumpPower * std::pow(stats.airJumpFalloff, (float)st.airJumpsUsed);
                    st.slideActive = false;
                }
            });
}
