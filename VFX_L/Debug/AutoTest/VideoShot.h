// ============================================================
// VideoShot.h
// TEMP-TEST: 作品紹介の動画の 1 カット（2026-10-23）。VFXL_BATTLE_AUTOTEST=video_<名前> + VFXL_CAPTURE=<出力>。
//
// 1 カット = 1 クラス。Setup で場面を作り（湧き・背嚢・敵）、Tick で毎フレーム動かし（プレイヤーの移動・追加の敵）、
// カメラはキーフレーム（時刻・向き・見下ろし・距離・注視点の高さ）を滑らかに補う。
// recStart〜recEnd 秒（Setup からの時間）だけ Core/VideoCapture が書き出し、終わったらプログラムを閉じる。
// 時計は書き出しの時計（1/60 秒ずつ）。スロー（SetSlow）はゲームの時間だけ遅くなり、カメラとこの時計は等速。
// HUD は普段どおりの数字（HP / MP は普段の上限のまま毎フレーム満タン + 無敵、レベルアップの画面は出さない）。
// ============================================================
#pragma once
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Core/VideoCapture.h"

class VideoShot : public BattleAutoTest
{
public:
    using BattleAutoTest::BattleAutoTest;
    void UpdateFrame(float dt) final;
    bool SelfClock() const final { return true; }

protected:
    struct CamKey { float t, yaw, pitch, dist, height; };

    virtual void Setup() = 0;
    virtual void Tick(float /*t*/, float /*dt*/) {}
    // 注視点を決め打ちする時は true を返して out に入れる（既定 = プレイヤー）
    virtual bool CameraTarget(float /*t*/, Vector3& /*out*/) { return false; }

    // ---- カットの設定（Setup の中で決める）----
    float recStart = 1.5f;   // Setup からこの秒で書き出しを始める（それまでは場面が落ち着くのを待つ）
    float recEnd = 9.0f;
    std::vector<CamKey> camKeys;
    bool hud = true;         // false = UI を丸ごと描かない
    int  level = 18;         // HUD に出すレベル
    int  gold = 260;
    float expFill = 0.55f;   // 経験のバー（0..1、これ以上は溜めない = レベルアップの画面を出さない）

    // ---- 道具 ----
    float Now() const { return m_T; }
    Vector3 PlayerPos();
    void StopSpawning();                 // 湧き・波・天候を止めて全消し
    void ClearSpells();                  // 背嚢を空にして 9×9 に枠を敷く
    int  PlaceAnywhere(ItemID id);       // 空いている所へ（置けた index、無ければ -1）
    void BuildShowcase();                // タイトルの戦いの見本と同じ並び + 機能ルーン（メテオ・魔導光線・毒・黄金の矢）
    int  PlaceRuneFor(ItemID rune, int target);   // target（背嚢の index）に届く所へルーンを置く（置けた index / -1）
    void SetCasting(bool on);
    // center の周り rMin〜rMax の歩けるマスに count 体（hp は 1 体分、speedMul は普段の速さに掛ける）
    void SpawnRing(const Vector3& center, int count, float rMin, float rMax, uint32_t kind, float hp, float speedMul,
        float arcCenterDeg = 0.0f, float arcHalfDeg = 180.0f);
    // 群れへメテオを 1 発（タイトルと同じ：着弾点を決めて落とす）
    void DropMeteor(const Vector3& impact);
    void FaceNearestEnemy(float dt, float turnDegPerSec = 360.0f);
    void SetSlow(float scale) { VideoCapture::Get().SetTimeScale(scale); }
    void Move(float x, float y) { m_PlayerControlSystem.testMove = Vector2(x, y); }
    float Rand01();

private:
    void ApplyCamera();
    void KeepHud();
    float m_T = -1.0f;   // Setup からの秒（Setup 前は負）
    float m_Boot = 0.0f; // 起動からの秒
    bool  m_Setup = false;
    bool  m_Done = false;
    uint32_t m_Rng = 0x2026A1u;
};
