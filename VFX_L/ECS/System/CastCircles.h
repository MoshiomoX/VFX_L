// ============================================================
// CastCircles.h
// 詠唱の魔法陣（2026-10-09 ユーザー：「プレイヤーの詠唱のエフェクトを足して」→ 足元の魔法陣・手の前の小さな魔法陣を選んだ）。
//   ・足元：撃っている間ずっと足元に回る魔法陣（花の魔法陣 + 内側に逆回りの星の魔法陣）。最後に撃った魔法の色へ寄っていき、
//     撃つたびに少し膨らむ。撃ち止めて holdTime 秒で消え始める。草に隠れないよう地面から footLift 上げる
//   ・手の前：撃った弾ごとに杖口の少し先へ、撃つ向きを向いた小さな星の魔法陣がぱっと開いて消える（弾はその真ん中から出る）
// 絵は UI の白い線画（UIDeco の MagicCircleFlower / MagicCircleStar）。VFXMeshRenderer へ平らな板として毎フレーム積む
// （ShieldBubble と同じやり方。json のエフェクトだと色・明るさを毎フレーム変えられない）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>
#include <vector>

class Model;
class VFXMeshRenderer;

class CastCircles
{
public:
    // 撃った（pos = 杖口、dir = 撃つ向き、color = 魔法の色（明るさは揃えてある））
    void OnCast(const DirectX::SimpleMath::Vector3& pos, const DirectX::SimpleMath::Vector3& dir,
        const DirectX::SimpleMath::Vector3& color);
    // feet = プレイヤーの足元。visible = false（死んだ等）で足元の魔法陣を消していく
    void Update(float dt, const DirectX::SimpleMath::Vector3& feet, bool visible);
    void Submit(VFXMeshRenderer& renderer);
    void DrawImGui();

    // ---- 見た目 ----
    bool  footEnabled = true;
    bool  handEnabled = true;
    float footSize = 2.6f;        // 直径 m
    float footLift = 0.25f;       // 地面から（草の上に出す）
    float footAlpha = 0.85f;
    float footIntensity = 1.6f;   // > 1 で bloom
    float footSpinDeg = 40.0f;    // 度/秒（内側の星は逆回り）
    float fadeIn = 0.12f;         // 秒
    float holdTime = 0.5f;        // 最後に撃ってから消え始めるまで
    float fadeOut = 0.4f;
    float pulse = 0.08f;          // 撃つたびの膨らみ（割合）
    float handSize = 0.75f;       // 直径 m（開き切った時）
    float handOffset = 0.25f;     // 杖口から撃つ向きへ
    float handLife = 0.28f;       // 秒
    float handIntensity = 2.0f;
    float handSpinDeg = 360.0f;

private:
    void EnsureResources();

    std::shared_ptr<Model> m_Quad;   // XZ の 1m 四方の板（法線 +Y、UV 0〜1）
    bool m_Ready = false;

    DirectX::SimpleMath::Vector3 m_Feet;
    float m_FootAct = 0.0f;          // 0〜1（足元の魔法陣の濃さ）
    float m_SinceCast = 1.0e9f;
    float m_FootSpin = 0.0f;
    float m_FootPulse = 0.0f;
    DirectX::SimpleMath::Vector3 m_FootColor = { 1.0f, 0.8f, 0.4f };
    DirectX::SimpleMath::Vector3 m_TargetColor = { 1.0f, 0.8f, 0.4f };

    struct Hand
    {
        DirectX::SimpleMath::Vector3 pos, dir, color;
        float age = 0.0f;
        float spin0 = 0.0f;
    };
    std::vector<Hand> m_Hands;
    float m_Clock = 0.0f;
};
