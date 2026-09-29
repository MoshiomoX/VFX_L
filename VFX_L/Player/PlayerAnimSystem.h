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
    // クリップ名（モデルを替える時はここを差し替える）。
    // 今は Quaternius の Universal Animation Library（Ranger.fbx に焼き込み済み）。
    // KayKit Mage の時は Idle / Running_A / Jump_Idle / Spellcast_Shoot / Hit_A / Death_A / Crouching / chest
    struct ClipNames
    {
        std::string idle = "Idle_Loop";
        std::string run = "Jog_Fwd_Loop";
        std::string jump = "Jump_Loop";
        std::string fall = "Jump_Loop";
        std::string cast = "Spell_Simple_Shoot";
        std::string hurt = "Hit_Chest";
        std::string dead = "Death01";
        std::string slide = "Slide_Loop";    // 本物の滑りの姿勢（UAL2）
        std::string upperRoot = "spine_02";  // 上半身マスクの根（UE 風の骨名）
    };

    void Update(Registry& reg, float dt);

    ClipNames& Names() { return m_Names; }

    // 滑りの姿勢: slide クリップ（屈んだ姿勢）を足元を軸に後ろへ傾ける（度）。0 で傾けない
    float slideLeanDeg = 0.0f;   // Slide_Loop は最初から後ろへ倒れた姿勢（KayKit の Crouching の時は 15）
    float leanSpeedDeg = 120.0f;   // 傾きの付け外しの速さ 度/秒

private:
    ClipNames m_Names;
};
