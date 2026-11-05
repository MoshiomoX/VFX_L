#pragma once
// ============================================================
// StickNav.h
// メニューを左スティックでも動かせるようにする（2026-11-05）。十字キーと同じ「押した瞬間」を作る部品。
//   Repeat(d)  : 倒した瞬間に 1 回、倒し続けると repeatDelay の後 repeatInterval ごと（項目を上下に選ぶ所）
//   Pressed(d) : 倒した瞬間だけ（ページ送り・Tier の切り替え）
//   Held(d)    : 倒している間ずっと（スライダーの様に、自分で連続させる所）
// メニューが出た時にスティックを倒したまま（走っていた）なら、一度戻すまで何もしない。
// しばらく Update されなかった（= メニューが閉じていた）ら、戻すのを待つ状態から始め直す。
// 向きは一度に 1 つ（大きく倒している軸）。上 = スティックの上 = 画面の上
// ============================================================
#include <cmath>
#include <SimpleMath.h>
#include "Manager/InputManager.h"
#include "UI/UIDeco.h"

class StickNav
{
public:
    enum Dir { kNone = 0, kUp, kDown, kLeft, kRight };

    float press = 0.5f;            // これより倒したら 1 回
    float release = 0.3f;          // 出た時に倒していたら、これより戻すまで見ない。倒している間はこれより戻すまで続ける
    float repeatDelay = 0.40f;     // 倒し続けて連続で動き始めるまで（秒）
    float repeatInterval = 0.15f;  // 連続で動く間隔

    // 毎フレーム、メニューの入力を見る所で一度だけ呼ぶ（dt は実時間）
    void Update(float dt)
    {
        const float now = UIDeco::Clock();
        if (now - m_LastUpdate > 0.2f) { m_Armed = false; m_Held = kNone; }   // 閉じていた
        m_LastUpdate = now;

        const DirectX::SimpleMath::Vector2 s = TestOn() ? TestValue() : InputManager::Get().GetPadLeftStick();
        m_Pressed = m_Repeat = kNone;
        if (!m_Armed && s.Length() < release) m_Armed = true;

        Dir d = kNone;
        if (m_Armed)
        {
            // 倒している向きは release まで戻すまで続ける（0.5 前後で行ったり来たりしない）
            if (m_Held != kNone && Along(s, m_Held) > release) d = m_Held;
            else
            {
                const float ax = std::fabs(s.x), ay = std::fabs(s.y);
                if (ax > press && ax > ay) d = (s.x > 0.0f) ? kRight : kLeft;
                else if (ay > press && ay >= ax) d = (s.y > 0.0f) ? kUp : kDown;
            }
        }

        if (d != m_Held)
        {
            m_Held = d;
            m_Timer = repeatDelay;
            m_Pressed = m_Repeat = d;
        }
        else if (d != kNone)
        {
            m_Timer -= (std::fmin)((std::fmax)(dt, 0.0f), 0.1f);
            if (m_Timer <= 0.0f)
            {
                m_Timer = repeatInterval;
                m_Repeat = d;
            }
        }
    }

    bool Pressed(Dir d) const { return m_Pressed == d; }
    bool Repeat(Dir d) const { return m_Repeat == d; }
    bool Held(Dir d) const { return m_Held == d; }

    // 倒したままでも動かないように戻す（メニューを開き直した時など）
    void Reset() { m_Armed = false; m_Held = kNone; m_Pressed = m_Repeat = kNone; }

    // TEMP-TEST: 自動テスト用にスティックの値を差し替える（全ての StickNav に効く。on = false で戻す）
    static void DebugSetStick(const DirectX::SimpleMath::Vector2& v, bool on) { TestValue() = v; TestOn() = on; }

private:
    static float Along(const DirectX::SimpleMath::Vector2& s, Dir d)
    {
        switch (d)
        {
        case kUp:    return s.y;
        case kDown:  return -s.y;
        case kLeft:  return -s.x;
        case kRight: return s.x;
        default:     return 0.0f;
        }
    }
    static DirectX::SimpleMath::Vector2& TestValue() { static DirectX::SimpleMath::Vector2 v; return v; }
    static bool& TestOn() { static bool on = false; return on; }

    bool  m_Armed = false;
    Dir   m_Held = kNone;
    Dir   m_Pressed = kNone;
    Dir   m_Repeat = kNone;
    float m_Timer = 0.0f;
    float m_LastUpdate = -100.0f;
};
