// ============================================================
// PlayerAnimSystem.h
// PlayerStateComponent（HSM 3 層）→ SkinnedAnimComponent（再生層）の写像。
//
//   Move   層 → base  : Idle / Run / Jump / Fall
//   Action 層 → upper : Casting（上半身だけ。走りながら撃てる）
//   Damage 層 → over  : Hurt / Dead（全身上書き）
//
//   ここはクリップ名を決めるだけ。時計は SkinnedAnimSystem、描画は RenderSystem。
//   状態機（PlayerStateSystem）はアニメを知らないまま。
//
//   実行順: PlayerStateSystem → WeaponSystem → ここ。
//   WeaponSystem の後に置くのは「今フレーム撃った」を
//   castAnimTimer == castAnimDuration で拾うため
// ============================================================
#pragma once
#include <string>

class Registry;
class SkinnedModel;

class PlayerAnimSystem
{
public:
    // クリップ名（モデルを替える時はここを差し替える）
    struct ClipNames
    {
        std::string idle = "Idle";
        std::string run = "Running_A";
        std::string jump = "Jump_Idle";
        std::string fall = "Jump_Idle";
        std::string cast = "Spellcast_Shoot";
        std::string hurt = "Hit_A";
        std::string dead = "Death_A";
        std::string upperRoot = "chest";   // 上半身マスクの根
    };

    void Update(Registry& reg, float dt);

    ClipNames& Names() { return m_Names; }

private:
    ClipNames m_Names;
};
