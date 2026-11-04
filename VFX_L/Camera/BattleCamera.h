// ============================================================
// BattleCamera.h
// 戦闘シーンのカメラ一式：FollowCamera 本体 + シーン側の決まりごと
//   ・遮蔽の射線は地形（Layer_Terrain）にだけ当てる
//   ・マウスの捕獲：普段はカーソルを隠して視点操作。Alt 単押しで出す / しまう（切り替え）。
//     UI・死亡・デバッグカメラの間は常に出す（呼ぶ側が cursorNeeded で伝える）
//   ・画面の揺れのきっかけ：被弾（減った HP）と、カメラを揺らす印（AreaProfile::cameraShake）の付いた範囲が出た数（GPU が数える）
//   ・Camera パネル
// ============================================================
#pragma once
#include "Camera/FollowCamera.h"
#include <cstdint>

class CollisionSystem;

class BattleCamera
{
public:
    // 画角と遮蔽の射線の相手（null なら遮蔽回避なし）。シーンの Init から
    void Init(float aspect, CollisionSystem* terrain);
    void Resize(float aspect);

    FollowCamera& Camera() { return m_Camera; }
    const FollowCamera& Camera() const { return m_Camera; }

    // 毎フレーム（UI の開閉が決まった後）。要求は毎フレーム出さないと InputManager が放す
    void UpdateMouseCapture(bool cursorNeeded);
    // カーソルを出した状態で始める（魔法の実験場：パネルを触るのが主）。Alt で戻せる
    void SetCursorFree(bool free) { m_CursorFree = free; }

    // 揺れのきっかけ。hpLost = このフレームに減った HP（0 なら何もしない）
    void OnPlayerHit(float hpLost);
    // count = このフレームに届いた「カメラを揺らす範囲」（爆発・光線）の数（SwarmSystem::ConsumeShakeAreas）
    void OnShakeAreas(uint32_t count);

    // 賢いカメラ（2026-11-02）に渡す今の戦況。シーンが Update の前に毎フレーム
    struct SmartInput
    {
        bool    enabled = false;        // 設定「カメラの自動調整」 && 演出の最中でない
        int     nearEnemies = 0;        // プレイヤーの 10m 以内の敵（GPU のリードバック、2〜3 フレーム遅れ）
        bool    bossAlive = false;
        Vector3 bossPos;
        bool    finalWave = false;
        bool    threat = false;         // 押し寄せ中（threatDir の方から来る）
        Vector3 threatDir;              // 水平、正規化
    };
    void SetSmartInput(const SmartInput& in) { m_Smart = in; }

    // 追従（物理の後、Flush の前）。target が null なら追従点を動かさない
    void Update(float dt, const Vector3* target);
    // TEMP-TEST: 追従点をプレイヤー以外に決め打ちする（動画の書き出しで竜巻などを映す）。null で戻す
    void SetTargetOverride(const Vector3* p) { m_HasOverride = (p != nullptr); if (p) m_Override = *p; }

    void DrawImGui();

    // 調整値の保存 / 読込（Assets/Data/Camera.json）。Init で自動で読む。無ければコードの既定値
    bool SaveSettings(const char* path = nullptr) const;
    bool LoadSettings(const char* path = nullptr);
    void ResetSettings();   // コードの既定値へ戻す（ファイルはそのまま）

private:
    // 読んだ値の辻褄（範囲の下限 > 上限を入れ替え、距離をホイールの範囲に入れる）。LoadSettings の最後
    void SanitizeSettings();
    FollowCamera m_Camera;
    bool m_CursorFree = false;   // Alt で出している
    bool m_HasOverride = false;  // SetTargetOverride
    Vector3 m_Override;

    // ---- 画面の揺れのきっかけ ----
    // どちらも trauma を「少なくともこの値まで」上げる（足さない。続けて来ても上限を超えない）
    // 被弾：min(max, base + 減った HP × perDamage)
    bool  m_ShakeOnHit = true;
    float m_HitTraumaBase = 0.30f;
    float m_HitTraumaPerDamage = 0.01f;
    float m_HitTraumaMax = 0.70f;
    // 爆発：min(max, 出た数 × perArea)（位置は来ないので距離では弱めない）。
    // 2026-10-03 に爆発だけを数えるようにしたので 1 個 0.12 → 0.25、上限 0.35 → 0.4
    // （以前は 1 個の爆発に命中の火花の範囲が何個も付いて来て、足し算で強く揺れていた）
    bool  m_ShakeOnArea = true;
    float m_AreaTrauma = 0.25f;
    float m_AreaTraumaMax = 0.40f;

    // ---- 賢いカメラ（2026-11-02）----
    // ① 群れの濃さで引く：crowdStart 体までは 0、crowdFull 体で crowdMaxDist（その間は直線）。Boss が居れば +bossExtraDist、最終ウェーブ +finalExtraDist
    // ② Boss を画面に入れる：Boss が bossFrameRange 以内なら注視点を Boss へ bossFrameBias だけ寄せ（最大 bossFrameMax）、
    //    間の距離 × bossSpanMul − bossSpanStart を足す（上限 bossSpanMax）
    // ③ 押し寄せの方へ向きを寄せる（パッドだけ・既定オフ）：右スティックを turnIdle 秒触っていない間、毎秒 turnRate 度まで
    void  UpdateSmart(float dt);
    SmartInput m_Smart;
    float m_CrowdStart = 15.0f;
    float m_CrowdFull = 80.0f;
    float m_CrowdMaxDist = 4.0f;
    float m_BossExtraDist = 2.0f;
    float m_FinalExtraDist = 1.0f;
    float m_BossFrameRange = 30.0f;
    float m_BossFrameBias = 0.3f;
    float m_BossFrameMax = 6.0f;
    float m_BossSpanMul = 0.35f;
    float m_BossSpanStart = 3.0f;
    float m_BossSpanMax = 5.0f;
    bool  m_ThreatTurn = false;
    float m_TurnRate = 10.0f;
    float m_TurnIdle = 3.0f;
    float m_StickIdleTime = 0.0f;
    float m_LastSmartDist = 0.0f;   // パネル表示用（この戦況で欲しい距離）
    Vector3 m_FollowCache;          // このフレームの追従点（Boss との差を取る）
};
