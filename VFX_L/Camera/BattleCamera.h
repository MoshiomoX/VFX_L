// ============================================================
// BattleCamera.h
// 戦闘シーンのカメラ一式：FollowCamera 本体 + 場面側の決まりごと
//   ・遮蔽の射線は地形（Layer_Terrain）にだけ当てる
//   ・マウスの捕獲：普段はカーソルを隠して視点操作。Alt 単押しで出す / しまう（切り替え）。
//     UI・死亡・デバッグカメラの間は常に出す（呼ぶ側が cursorNeeded で伝える）
//   ・画面の揺れのきっかけ：被弾（減った HP）と範囲攻撃の発生（GPU の aliveAreas の増分）
//   ・Camera 面板
// ============================================================
#pragma once
#include "Camera/FollowCamera.h"
#include <cstdint>

class CollisionSystem;

class BattleCamera
{
public:
    // 画角と遮蔽の射線の相手（null なら遮蔽回避なし）。場面の Init から
    void Init(float aspect, CollisionSystem* terrain);
    void Resize(float aspect);

    FollowCamera& Camera() { return m_Camera; }
    const FollowCamera& Camera() const { return m_Camera; }

    // 毎フレーム（UI の開閉が決まった後）。要求は毎フレーム出さないと InputManager が放す
    void UpdateMouseCapture(bool cursorNeeded);

    // 揺れのきっかけ。hpLost = このフレームに減った HP（0 なら何もしない）
    void OnPlayerHit(float hpLost);
    void OnAliveAreas(uint32_t aliveAreas);

    // 追従（物理の後、Flush の前）。target が null なら追従点を動かさない
    void Update(float dt, const Vector3* target);

    void DrawImGui();

    // 調整値の保存 / 読込（Assets/Data/Camera.json）。Init で自動で読む。無ければコードの既定値
    bool SaveSettings(const char* path = nullptr) const;
    bool LoadSettings(const char* path = nullptr);
    void ResetSettings();   // コードの既定値へ戻す（ファイルはそのまま）

private:
    FollowCamera m_Camera;
    bool m_CursorFree = false;   // Alt で出している

    // ---- 画面の揺れのきっかけ ----
    // 被弾：trauma = min(max, base + 減った HP × perDamage)
    bool  m_ShakeOnHit = true;
    float m_HitTraumaBase = 0.30f;
    float m_HitTraumaPerDamage = 0.01f;
    float m_HitTraumaMax = 0.70f;
    // 範囲攻撃：GPU の aliveAreas が増えた数 × perArea（位置は来ないので距離では弱めない）
    bool  m_ShakeOnArea = true;
    float m_AreaTrauma = 0.12f;
    float m_AreaTraumaMax = 0.35f;
    uint32_t m_PrevAliveAreas = 0;
};
