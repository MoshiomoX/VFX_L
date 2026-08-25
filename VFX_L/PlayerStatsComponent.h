// ============================================================
// PlayerStatsComponent.h
// プレイヤーの能力値（純データ）。
//
// ★roguelite の成長はここを書き換える。Scene も System も値を持たない。
//   従来は CollisionTestScene のメンバ変数に置き、毎フレーム
//   SetMoveSpeed / SetJumpPower で System へ注入していた。
//   それでは「+10% 移動速度」のような成長を書く場所が無く、
//   シーンを増やすたびに同じ値を書き直すことになる。
//
// ★魔法の数値が Items/*.h にあり、杖の内容が集約結果であるのと同じ理由で、
//   プレイヤーの数値も Entity が持つ。実行時の真実は常にここ。
// ============================================================
#pragma once

struct PlayerStatsComponent
{
    // --- 移動 ---
    float moveSpeed = 5.0f;
    float jumpPower = 8.0f;

    // --- 体格（衝突体と見た目の両方が参照する）---
    // ※成長対象ではない。調整用。
    float radius = 0.4f;
    float height = 1.0f;

    // --- ジャンプの再入防止 ---
    // ★isGrounded に頼らない。PhysicsSystem が同フレーム内で
    //   上書きするため、PlayerControlSystem 側で false を書いても消える。
    //   実際に二重ジャンプを防いでいたのは「移動で地面から離れる」偶然で、
    //   高フレームレートでは dt が小さく離れきらない可能性がある。
    float jumpCooldown = 0.0f;
};