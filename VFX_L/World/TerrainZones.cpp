// ============================================================
// TerrainZones.cpp
// 地形を建てる側（TerrainBuild）のうち、区域（平原 / 山頂 / 洞窟）の形から決まる物：
//   床の縦の壁になる箱、洞の岩の壁・屋根、洞の上に積む岩、洞の中の松明、三層の結果（山頂・洞窟のマス、一番奥）
// 生成（TerrainGenerator）と、区域を塗り替えた後の作り直し（MapTerrainEdit::RegenZones）が同じ関数を通る
// ============================================================
#include "World/TerrainBuild.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include <algorithm>

namespace TerrainBuild
{
    // マスの印（mask == value のマス）を軸平行の矩形の組に分ける（衝突の箱にする）。
    // 左下から横に伸ばし、同じ幅で上へ伸ばせるだけ伸ばす貪欲法
    std::vector<Rect> MaskRects(const std::vector<uint8_t>& mask, int w, int d, uint8_t value)
    {
        std::vector<uint8_t> used(mask.size(), 0);
        auto avail = [&](int x, int z) { const size_t i = (size_t)z * w + x; return mask[i] == value && !used[i]; };
        std::vector<Rect> out;
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
            {
                if (!avail(x, z)) continue;
                int x1 = x;
                while (x1 + 1 < w && avail(x1 + 1, z)) ++x1;
                int z1 = z;
                for (;;)
                {
                    const int nz = z1 + 1;
                    if (nz >= d) break;
                    bool ok = true;
                    for (int xx = x; xx <= x1 && ok; ++xx) ok = avail(xx, nz);
                    if (!ok) break;
                    z1 = nz;
                }
                for (int zz = z; zz <= z1; ++zz)
                    for (int xx = x; xx <= x1; ++xx) used[(size_t)zz * w + xx] = 1;
                out.push_back({ x, z, x1 - x + 1, z1 - z + 1 });
            }
        return out;
    }

    // ============================================================
    // 床の衝突の箱：洞窟以外は上面 plainTop の箱（底は洞窟の底より下 = 洞窟の壁にもなる）、
    // 洞窟の底は上面 -mineD、山頂は summitTop まで。どれもマスの印を矩形に分けた箱の組
    // ============================================================
    void EmitFloorBoxes(Emitter& emit, const GridWorld& origin, const std::vector<uint8_t>& zone,
        float floorBottom, float plainTop, float summitTop, float mineD)
    {
        const int gw = origin.Width(), gd = origin.Depth();
        std::vector<uint8_t> solid((size_t)gw * gd);
        for (size_t i = 0; i < solid.size(); ++i) solid[i] = (zone[i] == ReliefField::kMine) ? 0 : 1;
        for (const Rect& r : MaskRects(solid, gw, gd, 1))
        {
            const Vector3 lo = RectMin(origin, r);
            emit.Box({ lo.x, floorBottom, lo.z }, lo + Vector3(r.w * kCs, plainTop, r.d * kCs), Layer_Terrain);
        }
        for (const Rect& r : MaskRects(zone, gw, gd, ReliefField::kMine))
        {
            const Vector3 lo = RectMin(origin, r);
            emit.Box({ lo.x, floorBottom, lo.z }, lo + Vector3(r.w * kCs, -mineD, r.d * kCs), Layer_Terrain);
        }
        for (const Rect& r : MaskRects(zone, gw, gd, ReliefField::kSummit))
        {
            const Vector3 lo = RectMin(origin, r);
            emit.Box({ lo.x, floorBottom, lo.z }, lo + Vector3(r.w * kCs, summitTop, r.d * kCs), Layer_Terrain);
        }
    }

    // ============================================================
    // 洞の岩の壁 = 坑の 8 近傍の、坑でも洞の口でもないマス（外周の崖のマスは除く）
    // ============================================================
    void ComputeCaveRing(const std::vector<uint8_t>& zone, const std::vector<Rect>& mouths, int gw, int gd,
        std::vector<uint8_t>& ring, std::vector<uint8_t>& mouthMask)
    {
        ring.assign((size_t)gw * gd, 0);
        mouthMask.assign((size_t)gw * gd, 0);
        for (const Rect& m : mouths)
            for (int z = m.z; z < m.z + m.d; ++z)
                for (int x = m.x; x < m.x + m.w; ++x)
                    if (x >= 0 && z >= 0 && x < gw && z < gd) mouthMask[(size_t)z * gw + x] = 1;
        for (int z = 1; z < gd - 1; ++z)
            for (int x = 1; x < gw - 1; ++x)
            {
                const size_t i = (size_t)z * gw + x;
                if (zone[i] == ReliefField::kMine || mouthMask[i]) continue;
                bool nextToPit = false;
                for (int dz = -1; dz <= 1 && !nextToPit; ++dz)
                    for (int dx = -1; dx <= 1 && !nextToPit; ++dx)
                        nextToPit = zone[(size_t)(z + dz) * gw + (x + dx)] == ReliefField::kMine;
                if (nextToPit) ring[i] = 1;
            }
    }

    // ============================================================
    // 洞窟の屋根：岩の壁のマスを塞ぎ、壁・屋根・口の上の梁の衝突（collTop まで。見えない高さまで = 上に乗れない）と、
    // 屋根・梁の見た目（roofBottom〜roofTop の板）
    // ============================================================
    void EmitCaveRoof(Emitter& emit, GridWorld* grid, const GridWorld& origin, const std::vector<uint8_t>& zone,
        const std::vector<uint8_t>& ring, const std::vector<uint8_t>& mouthMask,
        float roofBottomY, float roofTopY, float collTop, const Vector4& cliffHigh)
    {
        const int gw = origin.Width(), gd = origin.Depth();
        for (int z = 1; z < gd - 1; ++z)
            for (int x = 1; x < gw - 1; ++x)
                if (ring[(size_t)z * gw + x]) emit.Block(grid, x, z, 1, 1);

        const Vector4 rockTop(cliffHigh.x * 1.1f, cliffHigh.y * 1.1f, cliffHigh.z * 1.1f, 1.0f);
        for (const Rect& r : MaskRects(ring, gw, gd, 1))
        {
            const Vector3 lo = RectMin(origin, r);
            emit.Box(lo, lo + Vector3(r.w * kCs, collTop, r.d * kCs), Layer_Terrain);
        }
        auto roofOver = [&](const std::vector<uint8_t>& mask, uint8_t value)
            {
                for (const Rect& r : MaskRects(mask, gw, gd, value))
                {
                    const Vector3 lo = RectMin(origin, r);
                    emit.Box(lo + Vector3(0.0f, roofBottomY, 0.0f), lo + Vector3(r.w * kCs, collTop, r.d * kCs), Layer_Terrain);
                    Vector3 v[8];
                    BoxVerts(v, lo + Vector3(0.0f, roofBottomY, 0.0f), lo + Vector3(r.w * kCs, roofTopY, r.d * kCs));
                    emit.Visual(v, rockTop, cliffHigh, TerrainSurface::Rock, TerrainSurface::Rock);   // 天井は上も横も岩
                }
            };
        roofOver(zone, ReliefField::kMine);   // 屋根
        roofOver(mouthMask, 1);               // 口の上の梁
    }

    // ============================================================
    // 洞の上の岩（低い山に見せる）：岩の塊（坑 + 岩の壁）のマスに 2x2 マス毎に 1 個、
    // 塊の縁からの距離（4 近傍の BFS）で真ん中ほど高く。乱数の引き方は生成と同じ順
    // ============================================================
    int EmitRoofRocks(Emitter& emit, const GridWorld& origin, const std::vector<uint8_t>& zone,
        const std::vector<uint8_t>& ring, float roofTopY, float rockMin, float rockMax,
        const std::vector<std::string>& modelPaths, std::mt19937& rng)
    {
        const int gw = origin.Width(), gd = origin.Depth();
        auto randi = [&](int a, int b) { return (b <= a) ? a : std::uniform_int_distribution<int>(a, b)(rng); };
        auto randf = [&](float a, float b) { return std::uniform_real_distribution<float>(a, (std::max)(a, b))(rng); };
        auto inMass = [&](int x, int z)
            {
                if (x < 0 || z < 0 || x >= gw || z >= gd) return false;
                const size_t i = (size_t)z * gw + x;
                return zone[i] == ReliefField::kMine || ring[i] != 0;
            };
        std::vector<int> dist((size_t)gw * gd, -1);
        std::vector<int> queue;
        const int ndx[4] = { 1, -1, 0, 0 }, ndz[4] = { 0, 0, 1, -1 };
        for (int z = 0; z < gd; ++z)
            for (int x = 0; x < gw; ++x)
            {
                if (!inMass(x, z)) continue;
                bool edge = false;
                for (int k = 0; k < 4 && !edge; ++k) edge = !inMass(x + ndx[k], z + ndz[k]);
                if (edge) { dist[(size_t)z * gw + x] = 0; queue.push_back(z * gw + x); }
            }
        int maxDist = 0;
        for (size_t qi = 0; qi < queue.size(); ++qi)
        {
            const int c = queue[qi], cx = c % gw, cz = c / gw;
            maxDist = (std::max)(maxDist, dist[(size_t)c]);
            for (int k = 0; k < 4; ++k)
            {
                const int nx = cx + ndx[k], nz = cz + ndz[k];
                if (!inMass(nx, nz) || dist[(size_t)nz * gw + nx] >= 0) continue;
                dist[(size_t)nz * gw + nx] = dist[(size_t)c] + 1;
                queue.push_back(nz * gw + nx);
            }
        }

        struct RockModel { const std::string* path; std::shared_ptr<Model> model; float unit; Vector3 lo, hi; };
        std::vector<RockModel> rocks;
        for (const auto& path : modelPaths)
            if (auto m = ResourceManager::Get().LoadModel(path))
                rocks.push_back({ &path, m, m->GetFileUnitScale(), m->GetBoundsMin(), m->GetBoundsMax() });

        int placed = 0;
        for (int z = 1; z < gd - 1 && !rocks.empty(); z += 2)
            for (int x = 1; x < gw - 1; x += 2)
            {
                const int gx = x + randi(0, 1), gz = z + randi(0, 1);   // 2x2 の区画の中でずらす
                if (!inMass(gx, gz)) continue;
                const RockModel& pm = rocks[randi(0, (int)rocks.size() - 1)];
                const float h = (pm.hi.y - pm.lo.y) * pm.unit;
                if (h < 0.1f) continue;
                const float t = (maxDist > 0) ? (float)dist[(size_t)gz * gw + gx] / (float)maxDist : 0.0f;
                const float targetH = (rockMin + (rockMax - rockMin) * t) * randf(0.8f, 1.2f);
                const Vector3 c = origin.CellToWorld(gx, gz);
                // 乱数の順は元の呼び出し（関数の引数。MSVC は右から評価する）に合わせてある：z のずれ → x のずれ → 向き
                const float offZ = randf(-1.0f, 1.0f);
                const float offX = randf(-1.0f, 1.0f);
                const float yawDeg = randf(0.0f, 360.0f);
                const float u = pm.unit * (targetH / h);
                const float ground = roofTopY - targetH * 0.15f;   // 少し埋める
                emit.Begin(MapData::kRoofRock);
                emit.Prop(*pm.path, pm.model, { c.x + offX, ground - pm.lo.y * u, c.z + offZ }, yawDeg, u);
                ++placed;
            }
        return placed;
    }

    // ============================================================
    // 洞の中の松明：坑の底のマスで、隣が塞がった壁（岩の壁・外周）の所。壁の面に付け、spacing マス毎。
    // 灯りの位置を outLights へ足す（点光源はシーンが近い物にだけ付ける）
    // ============================================================
    int EmitCaveTorches(Emitter& emit, const GridWorld& origin, const std::function<bool(int, int)>& pitFloor,
        const std::vector<uint8_t>& ring, float mineD, int spacing, bool rimRock, float rimSink,
        const std::string& torchPath, std::vector<Vector3>& outLights)
    {
        const int gw = origin.Width(), gd = origin.Depth();
        auto model = ResourceManager::Get().LoadModel(torchPath);
        if (!model) return 0;
        const float unit = model->GetFileUnitScale();
        const Vector3 lo = model->GetBoundsMin(), hi = model->GetBoundsMax();
        const float td = (hi.z - lo.z) * unit;
        const float yaws[4] = { 90.0f, 270.0f, 0.0f, 180.0f };   // 壁の向き +x / -x / +z / -z（遺跡の壁と同じ取り方）
        const int ndx[4] = { 1, -1, 0, 0 }, ndz[4] = { 0, 0, 1, -1 };
        const float gap = (float)(std::max)(spacing, 1) * kCs;
        std::vector<Vector3> placed;
        int count = 0;
        for (int z = 1; z < gd - 1; ++z)
            for (int x = 1; x < gw - 1; ++x)
            {
                if (!pitFloor(x, z)) continue;   // 歩ける底（坂・降り口・岩は除く）
                for (int k = 0; k < 4; ++k)
                {
                    const int nx = x + ndx[k], nz = z + ndz[k];
                    // 壁 = 洞の岩の壁か外周の崖のマス（外周の岩で塞いだマスの縁に付けると岩の前に浮く）
                    const bool wallCell = ring[(size_t)nz * gw + nx] != 0
                        || nx == 0 || nz == 0 || nx == gw - 1 || nz == gd - 1;
                    if (!wallCell) continue;
                    const Vector3 d((float)ndx[k], 0.0f, (float)ndz[k]);
                    const Vector3 wallP = origin.CellToWorld(x, z) + d * (kCs * 0.5f);
                    bool farEnough = true;
                    for (const Vector3& q : placed)
                        if ((q - wallP).LengthSquared() < gap * gap) { farEnough = false; break; }
                    if (!farEnough) continue;
                    // 外周の岩山は縁から外へ下がっているので、縁の線に付けると岩の前で宙に浮く：岩の中へ押し込む
                    // （深く入った所は隠れるが、灯りは元の位置に出す）
                    const float sink = (ring[(size_t)nz * gw + nx] == 0 && rimRock) ? rimSink : 0.0f;
                    const Vector3 p = wallP - d * (td * 0.5f);
                    const float u = unit * 1.0f;
                    emit.Begin(MapData::kTorch);
                    emit.Prop(torchPath, model, { p.x + d.x * sink, (-mineD + 2.0f) - lo.y * u, p.z + d.z * sink }, yaws[k], u);
                    outLights.push_back(Vector3(p.x, -mineD + 2.7f, p.z) - d * 0.4f);
                    placed.push_back(wallP);
                    ++count;
                    break;
                }
            }
        return count;
    }

    // ============================================================
    // 三層の結果：山頂・洞窟の歩けるマスと、洞窟の一番奥
    // （坂の降り口から底を 4 近傍で歩いた距離が一番大きく、周り 3x3 も底のマス）
    // ============================================================
    void ComputeLayoutCells(const GridWorld& origin, const std::function<bool(int, int)>& summitCell,
        const std::function<bool(int, int)>& mineFloor, const std::vector<Rect>& mineLandings, float mineD,
        std::vector<int>& summitCells, std::vector<int>& mineCells, bool& hasMineDeep, Vector3& mineDeep)
    {
        const int gw = origin.Width(), gd = origin.Depth();
        summitCells.clear();
        mineCells.clear();
        hasMineDeep = false;
        auto floorCell = [&](int x, int z) { return x >= 0 && z >= 0 && x < gw && z < gd && mineFloor(x, z); };
        for (int z = 0; z < gd; ++z)
            for (int x = 0; x < gw; ++x)
            {
                const int i = z * gw + x;
                if (summitCell(x, z)) summitCells.push_back(i);
                if (floorCell(x, z)) mineCells.push_back(i);
            }
        std::vector<int> dist((size_t)gw * gd, -1);
        std::vector<int> queue;
        for (const Rect& r : mineLandings)
            for (int z = r.z; z < r.z + r.d; ++z)
                for (int x = r.x; x < r.x + r.w; ++x)
                    if (floorCell(x, z) && dist[(size_t)z * gw + x] < 0)
                    {
                        dist[(size_t)z * gw + x] = 0;
                        queue.push_back(z * gw + x);
                    }
        int best = -1, bestDist = -1;
        for (size_t qi = 0; qi < queue.size(); ++qi)
        {
            const int c = queue[qi], cx = c % gw, cz = c / gw;
            bool roomy = true;
            for (int dz = -1; dz <= 1 && roomy; ++dz)
                for (int dx = -1; dx <= 1 && roomy; ++dx)
                    roomy = floorCell(cx + dx, cz + dz);
            if (roomy && dist[(size_t)c] > bestDist) { bestDist = dist[(size_t)c]; best = c; }
            const int nx[4] = { cx + 1, cx - 1, cx, cx }, nz[4] = { cz, cz, cz + 1, cz - 1 };
            for (int k = 0; k < 4; ++k)
                if (floorCell(nx[k], nz[k]) && dist[(size_t)nz[k] * gw + nx[k]] < 0)
                {
                    dist[(size_t)nz[k] * gw + nx[k]] = dist[(size_t)c] + 1;
                    queue.push_back(nz[k] * gw + nx[k]);
                }
        }
        if (best >= 0)
        {
            hasMineDeep = true;
            mineDeep = origin.CellToWorld(best % gw, best / gw);
            mineDeep.y = -mineD;
        }
    }
}
