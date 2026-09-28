// ============================================================
// PlayerStatsComponent.h
// プレイヤーの能力値（純データ）。
//
// ※roguelite の成長はここを書き換える。Scene も System も値を持たない。
//   従来は CollisionTestScene のメンバ変数に置き、毎フレーム
//   SetMoveSpeed / SetJumpPower で System へ注入していた。
//   それでは「+10% 移動速度」のような成長を書く場所が無く、
//   シーンを増やすたびに同じ値を書き直すことになる。
//
// ※魔法の数値が Items/*.h にあり、杖の内容が集約結果であるのと同じ理由で、
//   プレイヤーの数値も Entity が持つ。実行時の真実は常にここ。
// ============================================================
#pragma once

struct PlayerStatsComponent
{
    // --- 移動 ---
    float moveSpeed = 5.0f;
    float jumpPower = 8.0f;

    // --- 滑り（左 Ctrl / パッド X を押している間。PlayerControlSystem）---
    // 平地: 入った瞬間に moveSpeed * slideBoost まで押し出し、摩擦で減って slideMinSpeed を切ったら立つ。
    // 坂: 足元の面の法線から下り方向へ加速する（水平加速 = slideGravity * cosθ * sinθ）。
    // 跳んだら水平の勢いを持ったまま空中へ。勢い（moveSpeed を超えた分）は momentumDecay で減る
    float slideBoost = 1.6f;            // 平地で入った時の初速（moveSpeed の倍率）
    float slideBoostCooldown = 1.0f;    // 押し出しの再使用待ち 秒（連打で加速し続けない）
    float slideFriction = 5.0f;         // 滑っている間の減速 m/s^2
    float slideGravity = 25.0f;         // 坂の下り加速の強さ m/s^2（28 度の坂で水平 約 10）
    float slideMaxSpeed = 20.0f;        // m/s
    float slideMinSpeed = 3.0f;         // 接地中にこれを切ったら滑りをやめる m/s
    float slideTurnRate = 90.0f;        // 滑っている間に向きを変えられる速さ 度/秒
    float momentumDecay = 6.0f;         // 滑りをやめた後の勢いの減り方（地上）m/s^2
    float momentumDecayAir = 1.5f;      // 同（空中）
    float momentumTurnRate = 360.0f;    // 勢いが残っている間の向きの変え方 度/秒

    // --- 体格（衝突体と見た目の両方が参照する）---
    // ※成長対象ではない。調整用。
    float radius = 0.4f;
    float height = 1.0f;

    // --- ジャンプの再入防止 ---
    // ※isGrounded に頼らない。PhysicsSystem が同フレーム内で
    //   上書きするため、PlayerControlSystem 側で false を書いても消える。
    //   実際に二重ジャンプを防いでいたのは「移動で地面から離れる」偶然で、
    //   高フレームレートでは dt が小さく離れきらない可能性がある。
    float jumpCooldown = 0.0f;
};