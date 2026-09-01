// ============================================================
// PlayerStateComponent.h
// プレイヤー状態（純データ）。階層状態機（HSM）の3層構成。
//
//    層ごとに独立して遷移する。「走りながら杖を振る」は
//    Move=Run と Action=Casting が同時に成立している状態。
//    1つの enum に Run / Cast / RunCast … と並べると組合せ爆発するので分ける。
//
//    層間の関係は「優先度 + 抑制マスク」で表す。
//    上位層が下位層を抑制する。下位層は上位層を一切知らない
//   （知ってしまうと分層した意味が無くなる）。
//   これにより将来 Stunned / Knockback を足す時も
//   Move 層のコードに触らずに済む。
// ============================================================
#pragma once
#include <cstdint>

// ---- 層の識別（優先度そのもの。数値が大きいほど強い）----
enum class StateLayer : uint32_t
{
    Move   = 0,   // 最下位：移動と接地
    Action = 1,   // 中位：施法などの動作
    Damage = 2,   // 最上位：被弾と死亡
};

// ---- 抑制マスク（どの層を止めるか）----
enum LayerMask : uint32_t
{
    Mask_None   = 0,
    Mask_Move   = 1 << 0,
    Mask_Action = 1 << 1,
    Mask_All    = 0xFFFFFFFF,
};

// ============================================================
// 移動層：接地しているか、上下に動いているか
// ============================================================
enum class MoveStateID
{
    Idle,   // 接地・静止
    Run,    // 接地・移動中
    Jump,   // 空中・上昇中
    Fall,   // 空中・下降中
};

// ============================================================
// 動作層：杖を振っているか
// Casting は Move を抑制しない。

//   ここで移動を止めると走位という唯一の操作が奪われる。
// ============================================================
enum class ActionStateID
{
    None,
    Casting,
};

// ============================================================
// 被損層：無敵時間と死亡
// Hurt は Action を抑制する（被弾中は詠唱を止める）が Move は残す。
//   Dead は全部抑制する。
// ============================================================
enum class DamageStateID
{
    Normal,
    Hurt,
    Dead,
};

struct PlayerStateComponent
{
    // ---- 現在の状態 ----
    MoveStateID   move   = MoveStateID::Idle;
    ActionStateID action = ActionStateID::None;
    DamageStateID damage = DamageStateID::Normal;

    // ---- 各層の滞在時間（アニメーション再生位置と最小滞在時間の判定用）----
    float moveTime   = 0.0f;
    float actionTime = 0.0f;
    float damageTime = 0.0f;

    // ---- 被損層のタイマー ----
    float hurtDuration     = 0.25f;   // 被弾硬直の長さ
    float invincibleTimer  = 0.0f;    // これが 0 より大きい間は無敵
    float invincibleAfterHit = 0.6f;  // 被弾時に設定する無敵時間

    // ---- 遷移の記録（デバッグ表示用。実処理には使わない）----
    MoveStateID   prevMove   = MoveStateID::Idle;
    ActionStateID prevAction = ActionStateID::None;
    DamageStateID prevDamage = DamageStateID::Normal;

    // ============================================================
    // 現在の抑制マスク。上位層の状態から決まる。
    // 下位層はこれを見るのではなく、System が見て Update を飛ばす。
    //   下位層のコードに if (damage == Dead) を書かないための仕組み。
    // ============================================================
    uint32_t SuppressMask() const
    {
        switch (damage)
        {
        case DamageStateID::Dead: return Mask_All;
        case DamageStateID::Hurt: return Mask_Action;   // 移動は残す
        default: break;
        }
        return Mask_None;
    }

    bool IsSuppressed(LayerMask m) const { return (SuppressMask() & m) != 0; }
    bool IsDead()       const { return damage == DamageStateID::Dead; }
    bool IsInvincible() const { return invincibleTimer > 0.0f; }
};