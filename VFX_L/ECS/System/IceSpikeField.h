// ============================================================
// IceSpikeField.h
// アイススパイク（上級魔法、2026-10-10）の氷のトゲの見た目。
// ユーザー：「エルデンリングの霜踏みみたいに。大きさは敵の半分くらい」→「そんなに密にしないで、前へ 7〜8 本で十分」
//   →（同日）「本当は撃ち出す形。足元から狙った敵の少し奥まで、出ている間トゲが左右交互に。もう少し多く」。
//
// 撃った人の足元（start）から end まで、spacing 毎に 1 本、左右交互に少しずらして並べ、
// 足元から順に（waveSpeed で）地面から突き出て、しばらく残って沈む。
// トゲは特効モデル bingci_02.FBX（凍った敵の足元の氷と同じ物）。1 本ずつ地面の高さ（heightAt）に合わせ、
// 弾が消えた所の地面と maxStep 以上違う所（崖・台地の上下）には出さない。
// json のエフェクト（粒子）だと 1 本ずつ地面に合わせられず、出す数もぴったりにならないので、
// VFXMeshRenderer へ毎フレーム積む（ShieldBubble / CastCircles と同じやり方）。冷気・氷の欠片は IceSpike.json（end に出す）
// 判定（威力・凍結）は GPU の範囲（AreaData/IceSpikeBurst.json）で、ここは見た目だけ
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <functional>
#include <memory>
#include <random>
#include <vector>

class Model;
class VFXMeshRenderer;

class IceSpikeField
{
public:
    using HeightFn = std::function<float(float x, float z)>;

    // start = 撃った人の足元、end = 線の先（狙った敵の少し奥）。scale = 拡大鏡などの大きさの倍率
    void Spawn(const DirectX::SimpleMath::Vector3& start, const DirectX::SimpleMath::Vector3& end, float scale, const HeightFn& heightAt);
    void Update(float dt);
    void Submit(VFXMeshRenderer& renderer);
    void Clear() { m_Spikes.clear(); }
    int  ActiveCount() const { return (int)m_Spikes.size(); }   // 自動テスト用
    void DrawImGui();

    // ---- 並べ方 ----
    bool  enabled = true;
    float startGap = 0.6f;         // 足元から最初のトゲまで（m）
    float spacing = 0.5f;          // トゲの間隔（m。10m の線で約 19 本）
    float sideMin = 0.25f;         // 線から左右へずらす（m。1 本毎に左右が入れ替わる）
    float sideMax = 0.55f;
    float waveSpeed = 22.0f;       // 足元から先へ出ていく速さ（m/秒。10m で約 0.45 秒）
    float heightMin = 0.65f;       // トゲの高さ（m。雑魚 1.6m の半分くらい）
    float heightMax = 0.95f;
    float growFar = 1.12f;         // 線の先のトゲの高さの倍率（先へ行くほど少し大きく）
    float tiltDeg = 16.0f;         // 前へ傾ける（度。± tiltJitterDeg）
    float tiltJitterDeg = 8.0f;
    float maxStep = 0.8f;          // 線の真ん中の地面とこれ以上高さが違う所（横の崖・台地）には出さない（m）
    // ---- 時間 ----
    float riseTime = 0.10f;        // 突き出る
    float holdTime = 0.9f;         // 残る
    float sinkTime = 0.30f;        // 沈んで消える
    float sink = 0.12f;            // 立っている間も地面に埋めておく（m）
    float overshoot = 0.12f;       // 突き出る時に一瞬伸びる割合
    // ---- 色 ----（linear。1 を超えると bloom）
    DirectX::SimpleMath::Vector4 color = { 0.48f, 0.82f, 1.15f, 1.0f };
    float alpha = 0.85f;
    float intensity = 1.1f;
    float shade = 1.0f;            // 固定の光での 2 段の陰影 + 縁の光（VFXMeshPS）
    bool  toon = true;             // ガラスのような向きの帯（正面は透け、縁は明るい。シールドの護罩と同じ）

    // ---- 別のトゲに使う時（2026-10-24、砂の守護者の「砂の槍」= 石のトゲ。最初の Spawn までに入れる）----
    const char* modelPath = nullptr;   // null = 氷（Res::VFX::IceSpikes）
    int   blend = 1;                   // VFXMeshDrawItem::blend（0 加算 / 1 半透明）

private:
    void EnsureModel();

    struct Spike
    {
        DirectX::SimpleMath::Vector3 base;    // 根元（地面）
        DirectX::SimpleMath::Vector3 tiltAxis;
        float yaw = 0.0f;
        float tilt = 0.0f;                    // rad
        float height = 0.8f;
        float age = 0.0f;                     // 負 = まだ出ていない
    };
    std::vector<Spike> m_Spikes;

    std::shared_ptr<Model> m_Model;
    bool  m_Tried = false;
    float m_ModelHeight = 1.0f;
    DirectX::SimpleMath::Vector3 m_Anchor;    // 模型の底の中心
    std::mt19937 m_Rng{ 1357u };
};
