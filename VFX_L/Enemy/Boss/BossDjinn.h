// ============================================================
// BossDjinn.h
// 砂漠 3 面目の Boss「砂嵐の魔神」の数値と状態（2026-10-29、ユーザー「新しい Boss を作ろう」→ 砂嵐の魔神、二段目は「咆哮の後に技を入れ替える」）。
// BossAttacks.h が 500 行を越えていたので、この Boss の分はここに纏めて BossAttacks のメンバーにする（中身は BossAttacksDjinn.cpp）。
//   一段目：上半身だけの魔神が砂旋の上に浮く（hover m。GPU は幽霊のように壁・台地を越えて飛び、keepMin〜keepMax の間合いを取る）
//     ・砂嵐召喚：プレイヤーの足元を中心に砂旋が 3 本現れ、回りながら輪を縮める（輪の外へ走って出る）。砂旋は技の後も残る
//     ・炎の息：向きを固めて扇の予告 → 口から砂混じりの炎、扇がゆっくりプレイヤーを追う（横へ回り込む・扇の外へ）
//     ・砂渡り：砂に溶けて沈み（撃てない）、地面の下をプレイヤーの向こう側へ抜けて浮かび上がり、すぐ炎の息
//   二段目（狂暴の咆哮の瞬間に入れ替える）：降りて（hover → 0）地面を歩く。砂の拳 / 衝撃波 / 流沙 / 砂嵐召喚（4 本）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>
#include <vector>

// 調整値（Enemies パネルの「Sand Djinn」。BossAttacks::djinn）
struct DjinnTune
{
    // ---- 浮き・間合い（一段目）----
    float hover = 1.2f;            // 体を浮かせる高さ（世界 m）
    float landTime = 1.0f;         // 二段目へ入って降り切るまでの秒（咆哮の間に降りる）
    float keepMin = 8.0f;          // これより近いと下がる（AICS）
    float keepMax = 13.0f;         // ここまでは止まって撃つ（遠いと寄って来る）

    // ---- 砂嵐召喚（DustDevils）----
    int   devilCount = 3;
    int   devilCountPhase2 = 4;
    float devilCastTime = 1.3f;    // 魔神が止まって腕を上げている秒（砂旋はその間に育つ）
    float devilWindup = 1.0f;      // 砂旋が現れてから回り始めるまで（当たらない。足元の輪が育つ）
    float devilLife = 5.0f;        // 回る秒
    float devilOutro = 0.6f;       // ほどけて消える秒（当たらない）
    float devilRadiusStart = 8.5f; // 輪の半径（m）。始め → 終わり
    float devilRadiusEnd = 1.5f;
    float devilSpinDeg = 55.0f;    // 回る速さ（度 / 秒）
    float devilHitRadius = 1.6f;   // 砂旋の当たりの半径（+ プレイヤーの太さ）
    float devilReach = 3.0f;       // 足が地面からこれより上なら当たらない（砂旋は背が高い = ほぼ跳んでも当たる）
    float devilDamage = 18.0f;     // 1 本につき 1 回（高く吹き上げる）

    // ---- 炎の息（DjinnBreath）----
    float breathWindup = 1.0f;     // 溜め（腕を上げる。扇が育つ）
    float breathTime = 2.0f;       // 吐いている秒
    float breathRadius = 11.0f;    // 扇の半径 m
    float breathHalfDeg = 28.0f;   // 扇の半角（度）
    float breathTurnDeg = 30.0f;   // 吐いている間に扇がプレイヤーへ向く速さ（度 / 秒）
    float breathTick = 0.25f;      // 当たりを見る間隔（秒）
    float breathReach = 2.5f;      // 足が地面からこれより上なら当たらない
    float breathDamage = 10.0f;    // 1 回分
    float breathRest = 0.4f;       // 吐き終わってから次へ

    // ---- 砂渡り（DjinnShift）----
    float shiftSink = 0.45f;       // 砂に沈む秒
    float shiftSpeed = 22.0f;      // 地面の下を走る速さ m/s
    float shiftMaxTime = 1.4f;     // 走る秒の上限
    float shiftBehind = 7.0f;      // プレイヤーの向こう側のこの距離へ出る
    float shiftRise = 0.35f;       // 浮かび上がる秒（この後すぐ炎の息）

    // ---- 砂の拳（SandFist、二段目）----
    float fistWindup = 0.75f;      // 両腕を振り上げる（扇が育つ。向きは始めた瞬間に固める）
    float fistRadius = 6.5f;
    float fistHalfDeg = 50.0f;
    float fistDodgeHeight = 1.4f;  // 足が地面からこれより上なら当たらない
    float fistDamage = 30.0f;
    float fistSlamTime = 0.15f;    // 叩き付けの腕の振り下ろし（姿勢 1 → 1.3）
    float fistRest = 0.45f;
};

// 砂嵐召喚の砂旋 1 本（技の後も残る。BossAttacks::Devils()）
struct DjinnDevil
{
    DirectX::SimpleMath::Vector3 center;   // 輪の中心（始めた時のプレイヤーの足元、地面の高さ）
    DirectX::SimpleMath::Vector3 pos;      // 今の所（地面の高さ）
    float angle = 0.0f;                    // 中心から見た向き（rad）
    float radius = 8.5f;
    float age = 0.0f;                      // 現れてから
    float presence = 0.0f;                 // 0〜1（育つ / ほどける。見た目）
    bool  leaving = false;                 // ほどけている
    bool  hitDone = false;
    uint32_t id = 0;                       // シーンが見た目を対応させる番号（現れた順）
};

// シーンへの出力（BossAttacks::Output::djinn）
struct DjinnOutput
{
    // GPU の Boss へ（毎フレーム。BomberCB::bossHover / bossKeepMin / bossKeepMax / bossFly）
    float hover = 0.0f;
    float keepMin = 0.0f, keepMax = 0.0f;
    bool  fly = false;
    bool  landing = false;                 // 二段目へ入った瞬間（咆哮と同じフレーム。砂旋の体が崩れる見た目）
    bool  devilsSummoned = false;          // 砂旋が現れた瞬間（音）
    // 炎の息：吐いている間 on、向き（rad、+Z が 0）。started = 吐き始めた瞬間
    bool  breathStarted = false;
    bool  breathOn = false;
    float breathYaw = 0.0f;
    // 砂渡り
    bool  shiftSank = false;               // 沈み始めた瞬間
    bool  shiftTravel = false;             // 地面の下を走っている間（砂の流れを Boss に付ける）
    bool  shiftRose = false;               // 浮かび上がった瞬間
    // 砂の拳：叩き付けた瞬間（扇の真ん中あたりの地面）
    bool  fistSlammed = false;
    DirectX::SimpleMath::Vector3 fistPos;
};

// 技の途中の状態（BossAttacks の中だけ）
struct DjinnState
{
    bool  phase2 = false;
    float hoverBase = 0.0f;               // 降りる途中の高さ（世界 m）
    std::vector<DjinnDevil> devils;
    uint32_t nextDevilId = 1;
    // 炎の息
    DirectX::SimpleMath::Vector2 breathDir = { 0.0f, 1.0f };
    float breathTickLeft = 0.0f;
    bool  breathFired = false;
    // 砂渡り（骸の王の影へ消える も同じ）：0 沈む / 1 走る / 2 浮かぶ。秒と速さ・浮かんだ後の技は始めた時に決める（StartShift）
    float shiftSinkTime = 0.45f, shiftSpeedNow = 22.0f, shiftMaxNow = 1.4f, shiftRiseTime = 0.35f;
    int   shiftFollow = -1;   // BossMove（-1 = 無し）
    int   shiftPhase = 0;
    float shiftT = 0.0f;
    float shiftSink = 0.0f;
    DirectX::SimpleMath::Vector3 shiftTarget;
    // 砂の拳
    DirectX::SimpleMath::Vector2 fistDir = { 0.0f, 1.0f };
    bool  fistHitDone = false;
    // 技を始めた時の足元（扇の根元）
    DirectX::SimpleMath::Vector3 foot;
    float pose = 0.0f;                     // 腕の姿勢（Output::chargePose へ）
};
