// ============================================================
// FlowField.cpp
// Dial 法（整数コストの桶付きキュー）。
//   std::priority_queue 版は Debug で 1 万マス 26ms かかった（反復子検査）。
//   コストを整数（直進 10 / 斜め 14 / 高低差 1m = slopeCost×10）にして
//   環状の桶に積む。桶の数 > 最大辺コストなら正しい順で取り出せる。
//   松弛の時に親の向きも書くので、向き表を作る 2 周目は要らない
// ============================================================
#include "Swarm/FlowField.h"
#include <cfloat>
#include <cmath>
#include <algorithm>

using DirectX::SimpleMath::Vector2;

namespace
{
    constexpr int kBuckets = 128;          // > 最大辺コスト（14 + 斜面上乗せ）
    constexpr int kInf = 0x3fffffff;

    constexpr int dxs[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    constexpr int dzs[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    constexpr int stepCost[8] = { 10, 10, 10, 10, 14, 14, 14, 14 };
}

void FlowField::SetGrid(int w, int d, const std::vector<uint8_t>& walkable,
    const std::vector<float>& cellHeight)
{
    m_W = w;
    m_D = d;
    m_Walkable = walkable;
    m_Height = cellHeight;
    if ((int)m_Height.size() != w * d) m_Height.assign((size_t)w * d, 0.0f);
    m_Cost.clear();
    m_Dir.clear();
    m_TargetX = m_TargetZ = -1;
}

void FlowField::Build(int targetX, int targetZ)
{
    const int W = m_W, D = m_D, n = W * D;
    if (n <= 0) return;

    const uint8_t* walk = m_Walkable.data();
    const float* hgt = m_Height.data();
    auto walkable = [&](int x, int z) { return x >= 0 && x < W && z >= 0 && z < D && walk[z * W + x] != 0; };
    // 隣り合う 2 マスを行き来できるか（両方歩けて、段差が崖ほどでない）
    const float stepMax = maxStep, stepMaxDiag = maxStep * 1.41421356f;
    auto passable = [&](int ax, int az, int bx, int bz, bool diag)
        {
            if (!walkable(bx, bz)) return false;
            return std::fabs(hgt[bz * W + bx] - hgt[az * W + ax]) <= (diag ? stepMaxDiag : stepMax);
        };

    targetX = std::clamp(targetX, 0, W - 1);
    targetZ = std::clamp(targetZ, 0, D - 1);

    // 目標が壁の中（玩家が箱に乗っている等）なら、近くの通行可マスへ寄せる
    if (!walkable(targetX, targetZ))
    {
        bool found = false;
        for (int r = 1; r <= 3 && !found; ++r)
            for (int dz = -r; dz <= r && !found; ++dz)
                for (int dx = -r; dx <= r && !found; ++dx)
                    if (walkable(targetX + dx, targetZ + dz))
                    {
                        targetX += dx; targetZ += dz; found = true;
                    }
        if (!found) return;   // 完全に囲まれている。前回の場を使い続ける
    }
    m_TargetX = targetX;
    m_TargetZ = targetZ;

    // ---- 作業領域（生の配列。Debug の反復子検査を避ける）----
    static std::vector<int> cost;          // 整数コスト
    static std::vector<int> bucket[kBuckets];
    cost.assign(n, kInf);
    for (auto& b : bucket) b.clear();
    m_Dir.assign(n, Vector2(0, 0));
    int* costp = cost.data();
    Vector2* dirp = m_Dir.data();

    const int slopeUnit = (int)std::lround(slopeCost * 10.0f);
    const int t = targetZ * W + targetX;
    costp[t] = 0;
    bucket[0].push_back(t);

    // 探索の打ち切り。雑魚が居るのは湧き半径の内側だけなので、その外は
    // 向き (0,0)（= GPU 側が直線追跡）で構わない
    const int costLimit = (maxRangeCells > 0) ? maxRangeCells * 10 : kInf;

    int remaining = 1;
    int cur = 0;   // 今処理中のコスト
    while (remaining > 0 && cur <= costLimit)
    {
        auto& b = bucket[cur % kBuckets];
        // 桶は処理中に伸びない（同じコストへは積まれない: 辺コスト >= 10）
        for (size_t bi = 0; bi < b.size(); ++bi)
        {
            const int c = b[bi];
            --remaining;
            if (costp[c] != cur) continue;   // 古い項目（もっと安い経路で更新済み）

            const int cx = c % W, cz = c / W;
            for (int k = 0; k < 8; ++k)
            {
                const int nx = cx + dxs[k], nz = cz + dzs[k];
                if (!passable(cx, cz, nx, nz, k >= 4)) continue;
                // 斜めは両隣へも行ける時だけ（角を掠めて壁・崖に食い込まない）
                if (k >= 4 && (!passable(cx, cz, cx + dxs[k], cz, false)
                            || !passable(cx, cz, cx, cz + dzs[k], false))) continue;

                const int nc = nz * W + nx;
                const int dh = (int)std::lround(std::fabs(hgt[nc] - hgt[c]) * (float)slopeUnit);
                int edge = stepCost[k] + dh;
                if (edge >= kBuckets) edge = kBuckets - 1;   // 桶の周期を超えない（極端な段差）
                const int nCost = cur + edge;
                if (nCost < costp[nc])
                {
                    costp[nc] = nCost;
                    bucket[nCost % kBuckets].push_back(nc);
                    ++remaining;
                    // 向き = 親（今のマス）へ。正規化済みの 8 方向
                    const float sx = (float)-dxs[k], sz = (float)-dzs[k];
                    dirp[nc] = (k >= 4) ? Vector2(sx * 0.70710678f, sz * 0.70710678f) : Vector2(sx, sz);
                }
            }
        }
        b.clear();
        ++cur;
    }

    // デバッグ表示用に float へ
    m_Cost.resize(n);
    for (int i = 0; i < n; ++i)
        m_Cost[i] = (costp[i] == kInf) ? FLT_MAX : (float)costp[i] * 0.1f;
}
