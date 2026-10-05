// ============================================================
// PlayerStateComponent.h
// プレイヤー状態（純データ）。階層ステートマシン（HSM）の3層構成。
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
    Action = 1,   // 中位：詠唱などの動作
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
    Slide,  // 滑り中（PlayerControlSystem が slideActive を立てている間。坂の段差で一瞬浮いても続く）
};

// カメラの前から見た進む向き（前後左右 4 分割、各 ±45 度）。止まっている間は None
enum class MoveDirID
{
    None,
    Forward,
    Right,
    Back,
    Left,
};

// ============================================================
// 動作層：杖を振っているか
// Casting は Move を抑制しない。

//   ここで移動を止めると立ち回りという唯一の操作が奪われる。
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

    // ---- 滑り（PlayerControlSystem が書く。Move 層の Slide はこれを見る）----
    bool  slideActive = false;
    float slideAirTime = 0.0f;         // 滑り中に地面から離れている時間（短ければ滑りを続ける）
    float slideBoostTimer = 0.0f;      // 平地の押し出しの再使用待ち（0 で使える）
    bool  slideNeedsRelease = false;   // 遅くなって立った後はキーを離すまで滑り直さない（押しっぱなしで押し出しが繰り返されない）
    bool  carryMomentum = false;       // 滑りでついた moveSpeed 超えの勢いを持ち越している（普段の走りでは立たない）

    // ---- 空中の追加ジャンプ（PlayerControlSystem。接地で 0 に戻る）----
    int   airJumpsUsed = 0;

    // ---- ノックバック（2026-10-01。PlayerControlSystem::ApplyKnockback が入れ、Update が減らす）----
    // 初速 knockX/Z（水平 m/s）から knockDuration 秒かけて直線的に 0 へ。その間は入力の効きも 0 → 1 へ戻す
    float knockX = 0.0f, knockZ = 0.0f;
    float knockTime = 0.0f;            // 残り秒
    float knockDuration = 0.0f;
    float knockAppliedX = 0.0f, knockAppliedZ = 0.0f;   // 前フレームに速度へ足した分（滑りの勢いに混ざらないよう次で引く）

    // ---- 進む方向（PlayerControlSystem が書く）----
    // カメラの前から見た進む方向（度。0 = 前、+90 = 右、±180 = 後ろ）。体はこの方向へ振り向く
    float moveAngleCam = 0.0f;
    MoveDirID moveDir = MoveDirID::None;

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