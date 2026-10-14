// ============================================================
// FlowField.h
// 雑魚の巡路。プレイヤーのマスを目標に、通行マップ（GridWorld の walkable）上で
// Dijkstra を回し、各マスに「次に進む向き」を持たせる。
//
//   雑魚は自分のマスの向きを読むだけ（GPU、SwarmEnemyAICS）。
//   直線追跡だと箱の裏のプレイヤーに張り付いて動けないのを、これで回り込ませる。
//
//   コスト: 直進 1、斜め √2、高低差（斜面）に少し上乗せ。
//   斜め移動は両隣が通れる時だけ（角を掠めて壁に食い込むのを防ぐ）。
//   届かないマスは向き (0,0)（GPU 側は直線追跡へ落とす）。
//
//   プレイヤーのマスが変わった時に SwarmSystem が作り直す。全域は Debug で 200m（1 万マス）約 6ms、
//   300m（2.25 万マス）約 13ms（2026-10-02 / 03 実測）。打ち切ると崖の下の雑魚が坂を見つけられないので
//   全域のまま、別スレッドの作業用の場で作り、出来たら結果だけ貰う（TakeResult）。作業領域は場ごとに持つ（同時に 2 つ作れる）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <vector>
#include <cstdint>

class FlowField
{
public:
    // 格子の写し（UploadTerrain の時に一度渡す）。
    // fineHeight = GridWorld::Heights()（1 マスを sub × sub に分けた原始の高さ。空なら境の段差は見ない）
    void SetGrid(int w, int d, const std::vector<uint8_t>& walkable,
        const std::vector<float>& cellHeight,
        const std::vector<float>& fineHeight = {}, int sub = 0);

    // 目標マスから距離場と向きを作り直す。目標が通行不能なら最寄りの通行可マスに寄せる。
    // 寄せる先も無ければ何もしないで false（前の場のまま）
    bool Build(int targetX, int targetZ);

    // 定数（下の公開の値）を o から写す（作業用の場を本体と同じ設定で回す）
    void CopySettings(const FlowField& o);
    // o（同じ格子の場）が作った結果（向き・コスト・目標）を貰う。o の結果は古い物と入れ替わる
    void TakeResult(FlowField& o);

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

    // 隣のマス（中心の高さ）との差がこれを超えたら通れない = 台地の崖（m、斜めは √2 倍）。
    // 2m マスで 1.5m ≒ 37°。坂道（30° 以下 → 1 マス 1.15m 以下）は通り、崖は通らない。
    // GPU 側の SWARM_MAX_WALK_SLOPE（40°）と揃えておく
    float maxStep = 1.5f;

    // 境の段差（2026-10-02）：上下左右の隣とは、境を挟んだ原始の高さ（0.5m 刻み）の組も比べ、
    // これより大きい段があれば崖として扱う（上りは通れない、下りは飛び降りの規則）。
    // 中心の高さだけだと坂の横腹の麓に近い所（中心は 0.4〜1.1m）が通れることになり、
    // GPU（原始の高さで身体が崖に当たる）に止められた雑魚が坂の横に張り付いていた（自動テスト layers の登り）。
    // = GPU の SWARM_BODY_MAX_SLOPE × 半径 0.4 + SWARM_BODY_CLIFF_RISE ≒ 0.6
    float edgeStepMax = 0.6f;

    // 崖は上りだけ通れない（2026-10-01、ユーザー：台地の上の雑魚は下のプレイヤーへ縁から飛び降りる）。
    // 下りの崖は「そのマスが目標（プレイヤーのマス）より dropMinBelow m 以上高い」時だけ通れる
    // （= GPU の SwarmDropAllowed と同じ規則。プレイヤーも台地の上なら崖は今まで通り両向き通れない）。
    // 飛び降りの辺コスト = 1 マス分 + dropCost（坂へ回り道するより近ければ飛び降りる）
    bool  allowDrops = true;
    float dropMinBelow = 1.0f;
    int   dropCost = 5;

    // 探索を打ち切る経路長（マス数）。0 = 全域。
    // 雑魚は湧き半径の中にしか居ないので、その外まで解いても無駄
    int maxRangeCells = 0;

private:
    int m_W = 0, m_D = 0;
    int m_TargetX = -1, m_TargetZ = -1;
    std::vector<uint8_t> m_Walkable;
    std::vector<float>   m_Height;
    // 境の段差の表（SetGrid で 1 回作る。地形は静的）。[マス × 4 + 向き]（向き 0 +x / 1 -x / 2 +z / 3 -z の隣から
    // このマスへ）：上りの最大 / 下りの最大。原始の高さが無ければ空（境の段差は見ない）
    std::vector<float>   m_EdgeUp, m_EdgeDown;
    std::vector<float>   m_Cost;
    std::vector<DirectX::SimpleMath::Vector2> m_Dir;
    // 作業領域（生の配列で回す。Debug の反復子検査を避ける）
    std::vector<int> m_WorkCost;                  // 整数コスト
    std::vector<std::vector<int>> m_WorkBuckets;  // 環状の桶
};
