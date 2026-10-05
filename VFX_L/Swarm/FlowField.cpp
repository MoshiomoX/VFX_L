// ============================================================
// FlowField.cpp
// Dial 法（整数コストの桶付きキュー）。
//   std::priority_queue 版は Debug で 1 万マス 26ms かかった（反復子検査）。
//   コストを整数（直進 10 / 斜め 14 / 高低差 1m = slopeCost×10）にして
//   環状の桶に積む。桶の数 > 最大辺コストなら正しい順で取り出せる。
//   緩和の時に親の向きも書くので、向き表を作る 2 周目は要らない
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
    const std::vector<float>& cellHeight, const std::vector<float>& fineHeight, int sub)
{
    m_W = w;
    m_D = d;
    m_Walkable = walkable;
    m_Height = cellHeight;
    if ((int)m_Height.size() != w * d) m_Height.assign((size_t)w * d, 0.0f);
    // 境の段差の表：マス a と向き k の隣 b の境を挟んだ原始の高さの組（sub 組）のうち、
    // b から a への上り（a − b）の最大と下りの最大
    m_EdgeUp.clear();
    m_EdgeDown.clear();
    if (sub > 0 && fineHeight.size() == (size_t)w * sub * d * sub)
    {
        const int fineW = w * sub;
        m_EdgeUp.assign((size_t)w * d * 4, 0.0f);
        m_EdgeDown.assign((size_t)w * d * 4, 0.0f);
        for (int az = 0; az < d; ++az)
            for (int ax = 0; ax < w; ++ax)
                for (int k = 0; k < 4; ++k)
                {
                    const int bx = ax + dxs[k], bz = az + dzs[k];
                    if (bx < 0 || bz < 0 || bx >= w || bz >= d) continue;
                    float up = 0.0f, down = 0.0f;
                    for (int s = 0; s < sub; ++s)
                    {
                        int hxA, hzA, hxB, hzB;
                        if (dxs[k] != 0)
                        {
                            hxA = (dxs[k] > 0) ? ax * sub + sub - 1 : ax * sub;
                            hxB = (dxs[k] > 0) ? bx * sub : bx * sub + sub - 1;
                            hzA = hzB = az * sub + s;
                        }
                        else
                        {
                            hzA = (dzs[k] > 0) ? az * sub + sub - 1 : az * sub;
                            hzB = (dzs[k] > 0) ? bz * sub : bz * sub + sub - 1;
                            hxA = hxB = ax * sub + s;
                        }
                        const float rise = fineHeight[(size_t)hzA * fineW + hxA] - fineHeight[(size_t)hzB * fineW + hxB];
                        up = (std::max)(up, rise);
                        down = (std::max)(down, -rise);
                    }
                    m_EdgeUp[((size_t)az * w + ax) * 4 + k] = up;
                    m_EdgeDown[((size_t)az * w + ax) * 4 + k] = down;
                }
    }
    m_Cost.clear();
    m_Dir.clear();
    m_TargetX = m_TargetZ = -1;
}

void FlowField::CopySettings(const FlowField& o)
{
    slopeCost = o.slopeCost;
    maxStep = o.maxStep;
    edgeStepMax = o.edgeStepMax;
    allowDrops = o.allowDrops;
    dropMinBelow = o.dropMinBelow;
    dropCost = o.dropCost;
    maxRangeCells = o.maxRangeCells;
}

void FlowField::TakeResult(FlowField& o)
{
    m_Dir.swap(o.m_Dir);
    m_Cost.swap(o.m_Cost);
    m_TargetX = o.m_TargetX;
    m_TargetZ = o.m_TargetZ;
}

bool FlowField::Build(int targetX, int targetZ)
{
    const int W = m_W, D = m_D, n = W * D;
    if (n <= 0) return false;

    const uint8_t* walk = m_Walkable.data();
    const float* hgt = m_Height.data();
    auto walkable = [&](int x, int z) { return x >= 0 && x < W && z >= 0 && z < D && walk[z * W + x] != 0; };
    // 雑魚が b から a（目標に近い方）へ進めるか: b が歩けて、上りが崖ほどでない。
    // 下りの崖（飛び降り）は b が目標より dropMinBelow 以上高い時だけ（isDrop で返す）
    const float stepMax = maxStep, stepMaxDiag = maxStep * 1.41421356f;
    float targetH = 0.0f;   // 目標マスの高さ（下で決まる）
    // 上下左右の隣との境の段差（SetGrid で作った表）。b から a への上り / 下りの最大
    const float* edgeUp = m_EdgeUp.empty() ? nullptr : m_EdgeUp.data();
    const float* edgeDown = m_EdgeDown.empty() ? nullptr : m_EdgeDown.data();
    auto passableEx = [&](int ax, int az, int bx, int bz, bool diag, bool& isDrop)
        {
            isDrop = false;
            if (!walkable(bx, bz)) return false;
            const float ha = hgt[az * W + ax], hb = hgt[bz * W + bx];
            const float lim = diag ? stepMaxDiag : stepMax;
            float up = 0.0f, down = 0.0f;
            if (!diag && edgeUp)   // 斜めは両隣を通れる時だけ（呼ぶ側）
            {
                const int k = (bx > ax) ? 0 : (bx < ax) ? 1 : (bz > az) ? 2 : 3;
                up = edgeUp[(az * W + ax) * 4 + k];
                down = edgeDown[(az * W + ax) * 4 + k];
            }
            if (ha - hb > lim || up > edgeStepMax) return false;        // 上りの崖（中心の差・境の段）
            if (hb - ha <= lim && down <= edgeStepMax) return true;     // 坂・平地
            isDrop = allowDrops && (hb - targetH >= dropMinBelow);   // 下りの崖
            return isDrop;
        };

    targetX = std::clamp(targetX, 0, W - 1);
    targetZ = std::clamp(targetZ, 0, D - 1);

    // 目標が壁の中（プレイヤーが箱に乗っている等）なら、近くの通行可マスへ寄せる
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
        if (!found) return false;   // 完全に囲まれている。前回の場を使い続ける
    }
    m_TargetX = targetX;
    m_TargetZ = targetZ;
    targetH = hgt[targetZ * W + targetX];

    // ---- 作業領域（場ごと。別スレッドで作る作業用の場と本体が同時に作れる）----
    std::vector<int>& cost = m_WorkCost;
    if ((int)m_WorkBuckets.size() != kBuckets) m_WorkBuckets.resize(kBuckets);
    std::vector<int>* bucket = m_WorkBuckets.data();
    cost.assign(n, kInf);
    for (int i = 0; i < kBuckets; ++i) bucket[i].clear();
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
                bool drop = false;
                if (!passableEx(cx, cz, nx, nz, k >= 4, drop)) continue;
                // 斜めは両隣を経由しても行ける時だけ（角を掠めて壁・崖に食い込まない）。飛び降りは真っ直ぐだけ。
                // 両隣 → 目標側（c）に加えて、自分（n）→ 両隣も見る（2026-10-05）。以前は前者だけで、
                // 起伏で台地の角の斜め隣・坂の脇の地面が上がると「中心の差 < 1.5m × √2」で斜めに通れることになり、
                // 実際は角から隣へ 1.9m の崖 / 坂の脇の 0.8m の段で、GPU は止める → 雑魚がその場で詰まった（soak）
                if (k >= 4)
                {
                    if (drop) continue;
                    const int ix1 = cx + dxs[k], iz1 = cz;   // 中継のマス（x だけ進んだ所）
                    const int ix2 = cx, iz2 = cz + dzs[k];   // 中継のマス（z だけ進んだ所）
                    bool d1 = false, d2 = false, d3 = false, d4 = false;
                    if (!passableEx(cx, cz, ix1, iz1, false, d1) || d1
                        || !passableEx(cx, cz, ix2, iz2, false, d2) || d2) continue;   // 中継 → c
                    if (!passableEx(ix1, iz1, nx, nz, false, d3) || d3
                        || !passableEx(ix2, iz2, nx, nz, false, d4) || d4) continue;   // n → 中継
                }

                const int nc = nz * W + nx;
                const int dh = drop ? dropCost
                    : (int)std::lround(std::fabs(hgt[nc] - hgt[c]) * (float)slopeUnit);
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
    return true;
}
