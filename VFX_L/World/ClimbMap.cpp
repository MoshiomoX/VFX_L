// ============================================================
// ClimbMap.cpp
// 雑魚が登れる障害物の表（説明は ClimbMap.h）
// ============================================================
#include "World/ClimbMap.h"
#include "World/GridWorld.h"
#include "World/MapData.h"

namespace
{
    // 衝突の上面を登れる物にする記録の種類。床・外周・場外・崖の衝突（段々の台地の縁の薄い箱）・洞は入れない
    bool ClimbableKind(uint16_t kind)
    {
        switch (kind)
        {
        case MapData::kTree:
        case MapData::kRock:
        case MapData::kBush:
        case MapData::kRuinWall:
        case MapData::kRoofRock:
        case MapData::kManual:
        case MapData::kPillar:
        case MapData::kTerrace:
        case MapData::kPlateau:
            return true;
        default:
            return false;
        }
    }
    // 地面よりこれ以上高い上面だけを障害物とみなす（地面すれすれの箱は無視）
    constexpr float kMinRise = 0.3f;
}

ClimbMap::Stats ClimbMap::Build(const MapData::Map& map, GridWorld& grid)
{
    Stats st;
    const int W = grid.Width(), D = grid.Depth();
    const int sub = GridWorld::kHeightSub;
    const int HW = grid.HeightW(), HD = grid.HeightD();
    if (W <= 0 || D <= 0) { grid.ClearClimb(); return st; }

    // ---- 外周：縁のマスから塞いだマスだけを辿って届く物（4 近傍）----
    std::vector<uint8_t> outer((size_t)W * D, 0);
    std::vector<int> queue;
    queue.reserve((size_t)W * 4);
    auto seed = [&](int x, int z)
        {
            const size_t i = (size_t)z * W + x;
            if (outer[i] || grid.IsWalkable(x, z)) return;
            outer[i] = 1;
            queue.push_back((int)i);
        };
    for (int x = 0; x < W; ++x) { seed(x, 0); seed(x, D - 1); }
    for (int z = 0; z < D; ++z) { seed(0, z); seed(W - 1, z); }
    for (size_t qi = 0; qi < queue.size(); ++qi)
    {
        const int c = queue[qi], cx = c % W, cz = c / W;
        const int nx[4] = { cx + 1, cx - 1, cx, cx }, nz[4] = { cz, cz, cz + 1, cz - 1 };
        for (int k = 0; k < 4; ++k)
            if (nx[k] >= 0 && nx[k] < W && nz[k] >= 0 && nz[k] < D) seed(nx[k], nz[k]);
    }
    st.outerCells = (int)queue.size();

    // ---- 衝突の上面を、場内の塞いだマスの細かいマスへ ----
    std::vector<float> tops((size_t)HW * HD, GridWorld::kNoClimb);
    const float s = GridWorld::kCellSize / (float)sub;
    auto stamp = [&](float x0, float z0, float x1, float z1, float top)
        {
            // 足元の矩形に中心が入る細かいマス
            const int hx0 = (std::max)(0, (int)std::ceil((x0 - grid.OriginX()) / s - 0.5f));
            const int hx1 = (std::min)(HW - 1, (int)std::floor((x1 - grid.OriginX()) / s - 0.5f));
            const int hz0 = (std::max)(0, (int)std::ceil((z0 - grid.OriginZ()) / s - 0.5f));
            const int hz1 = (std::min)(HD - 1, (int)std::floor((z1 - grid.OriginZ()) / s - 0.5f));
            for (int hz = hz0; hz <= hz1; ++hz)
                for (int hx = hx0; hx <= hx1; ++hx)
                {
                    const int gx = hx / sub, gz = hz / sub;
                    if (grid.IsWalkable(gx, gz) || outer[(size_t)gz * W + gx]) continue;   // 歩ける所・外周は触らない
                    if (top < grid.HeightAt(hx, hz) + kMinRise) continue;
                    float& t = tops[(size_t)hz * HW + hx];
                    t = (std::max)(t, top);
                }
        };
    for (const MapData::Box& b : map.boxes)
        if (ClimbableKind(b.tag.kind)) stamp(b.lo.x, b.lo.z, b.hi.x, b.hi.z, b.hi.y);
    for (const MapData::Hull& h : map.hulls)
    {
        if (!ClimbableKind(h.tag.kind)) continue;
        float x0 = h.v[0].x, x1 = h.v[0].x, z0 = h.v[0].z, z1 = h.v[0].z, y1 = h.v[0].y;
        for (int k = 1; k < 8; ++k)
        {
            x0 = (std::min)(x0, h.v[k].x); x1 = (std::max)(x1, h.v[k].x);
            z0 = (std::min)(z0, h.v[k].z); z1 = (std::max)(z1, h.v[k].z);
            y1 = (std::max)(y1, h.v[k].y);
        }
        stamp(x0, z0, x1, z1, y1);
    }

    // ---- 上面の分かった細かいマスが 1 つでもある塞いだマス = 雑魚は通れる ----
    std::vector<uint8_t> cells((size_t)W * D, 0);
    for (int hz = 0; hz < HD; ++hz)
        for (int hx = 0; hx < HW; ++hx)
        {
            const float t = tops[(size_t)hz * HW + hx];
            if (t <= GridWorld::kNoClimb * 0.5f) continue;
            ++st.raisedFine;
            st.maxTop = (std::max)(st.maxTop, t);
            uint8_t& c = cells[(size_t)(hz / sub) * W + hx / sub];
            if (!c) { c = 1; ++st.cells; }
        }
    grid.SetClimb(cells, tops);
    return st;
}
