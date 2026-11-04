#pragma once
// ============================================================
// FlowDebugView.h
// 流れの見える化（2026-10-28、ユーザー：「風と敵の向きの流れ図を、ゲームの中でスイッチで出せるように」→
// 「見せるだけ・敵は経路の場と実際の動きの両方」、表示は「地図の上ではなく、ミニマップのような窓で」）。
// F8 で 切 → 風 → 経路の流れ場 → 群れの実際の動き → 切 と回す。画面の左の真ん中に、プレイヤーを中心にした
// 北が上の正方形の窓（右上のミニマップと同じ向き：+z が上、+x が左）を ImGui の前面に描く（Debug ビルドだけ）。
//   下地     2m のマスを 青 → 白 → 赤 で塗る（2026-10-28 ユーザー：「風の強さ・場の高低で青赤の差を」）：
//            風の時 = 風速（0 → windHeatMax）、他 = 窓の中の歩ける所の高さ（低 → 高）。塞がったマスは暗い灰、水は紺。
//            窓の下に色の目盛りと両端の値。矢印は白（黒い縁）
//   風       世界の風（WindState::Velocity）+ 局所の風源（竜巻・渦・突風・砂旋。SwarmSystem::GetLocalWind、2〜3 フレーム遅れ）。
//            マスの真ん中毎、長さ = 風速
//   経路     雑魚が歩く向きの表（SwarmSystem::GetFlowField。2m のマス毎にプレイヤーへ向かう向き）。長さは一定
//   群れ     雑魚の今の速度（DebugReadEnemies で crowdInterval 秒毎に読み戻し）を 2m のマス毎に平均。
//            長さ = 平均の速さ、色 = 数（白 → 黄）。雑魚 1 体ずつの点（黄）も描く
// ============================================================
#include <SimpleMath.h>

#include <cstdint>
#include <vector>

class GridWorld;
class SwarmSystem;
struct WindState;
struct LocalWind;

class FlowDebugView
{
public:
    using Vector2 = DirectX::SimpleMath::Vector2;
    using Vector3 = DirectX::SimpleMath::Vector3;

    enum class Mode { Off, Wind, Path, Crowd, Count };

    // F8 で次の表示へ。変わったら true
    bool HandleKey();
    Mode GetMode() const { return m_Mode; }
    void SetMode(Mode m) { m_Mode = m; m_CrowdTimer = 0.0f; }
    const char* ModeName() const;

    struct Inputs
    {
        const Vector3* player = nullptr;
        float playerYawDeg = 0.0f;   // 0 = +z（窓の上）
        const GridWorld* grid = nullptr;
        const WindState* wind = nullptr;
        float windTime = 0.0f;
        const LocalWind* local = nullptr;
        SwarmSystem* swarm = nullptr;
        float dt = 0.0f;
    };
    // 今の表示の窓を ImGui の前面に描く（毎フレーム。Off / ImGui 無しなら何もしない）
    void Draw(const Inputs& in);

    // 調整値
    float range = 30.0f;         // プレイヤーから窓の端まで m
    float panelSize = 380.0f;    // 窓の一辺 px（画面の高さ 900 の時。高さに比例）
    float crowdInterval = 0.25f; // 群れの読み戻しの間隔 秒（読み戻しは GPU を待つので毎フレームはしない）
    float windHeatMax = 8.0f;    // 下地の色：風の時、この風速 m/s で一番赤（ふだんの風 1.2〜2 は青寄り、竜巻の周りが赤）
    float heatDim = 0.62f;       // 下地の色の明るさ（白い矢印が見えるように暗め）

private:
    void ReadCrowd(const Inputs& in);

    Mode m_Mode = Mode::Off;

    // 群れ：最後に読んだ時のマス毎の速度の和と数（プレイヤーの周りの窓）と 1 体ずつの位置
    float m_CrowdTimer = 0.0f;
    int m_CrowdX0 = 0, m_CrowdZ0 = 0, m_CrowdN = 0;
    std::vector<Vector2> m_CrowdSum;
    std::vector<int> m_CrowdCount;
    std::vector<Vector2> m_CrowdDots;
};
