// ============================================================
// PlayerStateSystem.h
// プレイヤーの階層状態機を進める。
//
// ★呼ぶ位置は PhysicsSystem の直後、WeaponSystem の前。
//   isGrounded / velocity が今フレームの最終値になっているため。
//   PlayerControlSystem の位置（物理の前）で判定すると
//   接地状態が1フレーム古くなり、着地の見た目がずれる。
//
// ★この System は状態を書くだけ。
//   移動も施法もさせない（それぞれ PlayerControlSystem / WeaponSystem の担当）。
//   唯一の例外は Dead 時の velocity 停止で、これは
//   「死んだのに滑り続ける」を防ぐために必要。
// ============================================================
#pragma once

class Registry;

class PlayerStateSystem
{
public:
    void Update(Registry& reg, float dt);

    // 被弾を通知する。無敵中なら false を返し、ダメージ側は何もしない。
    // ★HitEvent を消費する側から呼ぶ。
    static bool TryApplyHit(Registry& reg, unsigned int entity, float damage);
};