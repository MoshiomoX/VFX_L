// ============================================================
// PlayerAnimSystem.h
// PlayerStateComponent（HSM 3 層）→ SkinnedAnimComponent（再生層）の写像。
//
//   Move   層 → base  : Idle / Run / Jump / Fall
//                       Run は水平の速さで Walk / Jog / Sprint を選び、
//                       再生速度を 実速度 / クリップの素の速さ にする（足の滑りを消す）
//   Action 層 → upper : Casting（上半身だけ。走りながら撃てる）
//   Damage 層 → over  : Hurt / Dead（全身上書き）
//
//   ここはクリップ名を決めるだけ。時計は SkinnedAnimSystem、描画は RenderSystem。
//   ステートマシン（PlayerStateSystem）はアニメを知らないまま。
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
        std::string walk = "Walk_Loop";
        std::string run = "Jog_Fwd_Loop";
        std::string sprint = "Sprint_Loop";
        std::string jump = "Jump_Loop";
        std::string fall = "Jump_Loop";
        std::string cast = "Spell_Simple_Shoot";
        std::string hurt = "Hit_Chest";
        std::string dead = "Death01";
        std::string slide = "Slide_Loop";    // 本物の滑りの姿勢（UAL2）
        std::string upperRoot = "spine_02";  // 上半身マスクの根（UE 風の骨名）
        std::string upperRootAlt = "CC_Base_Spine01";   // 無ければこちら（CC 骨の人形、2026-10-04）
    };

    void Update(Registry& reg, float dt);

    ClipNames& Names() { return m_Names; }

    // 滑りの姿勢: slide クリップ（屈んだ姿勢）を足元を軸に後ろへ傾ける（度）。0 で傾けない
    float slideLeanDeg = 0.0f;   // Slide_Loop は最初から後ろへ倒れた姿勢（KayKit の Crouching の時は 15）
    float leanSpeedDeg = 120.0f;   // 傾きの付け外しの速さ 度/秒

    // ---- 歩様（Run 状態の中の Walk / Jog / Sprint）----
    // クリップの素の速さ（m/秒）。UAL の *_RM.glb の root 移動量 / 1 周の長さで測った値
    float walkRefSpeed = 0.975f;    // Walk_Loop  1.30m / 1.333s
    float jogRefSpeed = 5.36f;      // Jog_Fwd_Loop 5.0m / 0.933s
    float sprintRefSpeed = 8.25f;   // Sprint_Loop 5.5m / 0.667s
    // 切り替えの速さ（m/秒）。行き来でばたつかないよう ± gaitHysteresis/2 の幅を持たせる
    float walkToJog = 2.3f;
    float jogToSprint = 6.7f;
    float gaitHysteresis = 0.4f;
    // 再生速度の範囲（境目の近くは多少滑るが、極端な早回し / 遅回しはしない）
    float minPlayRate = 0.5f;
    float maxPlayRate = 2.0f;

    int CurrentGait() const { return m_Gait; }   // 0 Walk / 1 Jog / 2 Sprint（パネルの表示用）
    float CurrentPlayRate() const { return m_PlayRate; }

private:
    ClipNames m_Names;
    int   m_Gait = 1;
    float m_PlayRate = 1.0f;
};
