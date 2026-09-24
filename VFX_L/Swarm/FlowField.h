// ============================================================
// FlowField.h
// 雑魚の巡路。玩家のマスを目標に、通行図（GridWorld の walkable）上で
// Dijkstra を回し、各マスに「次に進む向き」を持たせる。
//
//   雑魚は自分のマスの向きを読むだけ（GPU、SwarmEnemyAICS）。
//   直線追跡だと箱の裏の玩家に張り付いて動けないのを、これで回り込ませる。
//
//   コスト: 直進 1、斜め √2、高低差（斜面）に少し上乗せ。
//   斜め移動は両隣が通れる時だけ（角を掠めて壁に食い込むのを防ぐ）。
//   届かないマスは向き (0,0)（GPU 側は直線追跡へ落とす）。
//
//   1 万マスの Dijkstra は Debug でも 1ms 前後。毎フレームは要らないので
//   SwarmSystem が数フレームおき・玩家のマスが変わった時に作り直す
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <vector>
#include <cstdint>

class FlowField
{
public:
    // 格子の写し（UploadTerrain の時に一度渡す）
    void SetGrid(int w, int d, const std::vector<uint8_t>& walkable,
        const std::vector<float>& cellHeight);

    // 目標マスから距離場と向きを作り直す。目標が通行不能なら最寄りの通行可マスに寄せる
    void Build(int targetX, int targetZ);

    int  Width() const { return m_W; }
    int  Depth() const { return m_D; }
    bool IsBuilt() const { return !m_Dir.empty(); }
    int  TargetX() const { return m_TargetX; }
    int  TargetZ() const { return m_TargetZ; }

    // マス毎の向き（xz、正規化済み。届かない / 目標マス = (0,0)）。GPU へそのまま上げる
    const std::vector<DirectX::SimpleMath::Vector2>& Directions() const { return m_Dir; }
    // マス毎の到達コスト（デバッグ表示用。届かない = FLT_MAX）
    const std::vector<float>& Costs() const { return m_Cost; }

    // 斜面の上り下りをどれだけ嫌うか（高低差 1m あたりのコスト）
    float slopeCost = 1.5f;

    // 探索を打ち切る経路長（マス数）。0 = 全域。
    // 雑魚は湧き半径の中にしか居ないので、その外まで解いても無駄
    int maxRangeCells = 0;

private:
    int m_W = 0, m_D = 0;
    int m_TargetX = -1, m_TargetZ = -1;
    std::vector<uint8_t> m_Walkable;
    std::vector<float>   m_Height;
    std::vector<float>   m_Cost;
    std::vector<DirectX::SimpleMath::Vector2> m_Dir;
};
