// ============================================================
// TerrainBoulders.cpp
// 外周の岩山と洞窟の上の山を、数個の巨石（Rock-Set）を拡大して作る（2026-10-06、ユーザー：
// 岩を何十個も積むのでなく、1 個か数個の石を大きくして山に見せたい）。
// 石は軸ごとに違う倍率で引き伸ばす（MapData::Prop::stretch）：辺に沿って長く、外へ厚く、上へ高く。
// 衝突・格子は外周の崖の箱・洞の屋根の箱のまま（石は見た目だけ）
// ============================================================
#include "World/TerrainBuild.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Model/Model.h"

namespace TerrainBuild
{
    namespace
    {
        struct Rock { const std::string* path; std::shared_ptr<Model> model; float unit; Vector3 lo, hi; };

        std::vector<Rock> LoadRocks(const std::vector<std::string>& paths)
        {
            std::vector<Rock> out;
            for (const auto& path : paths)
                if (auto m = ResourceManager::Get().LoadModel(path))
                {
                    const Vector3 lo = m->GetBoundsMin(), hi = m->GetBoundsMax();
                    if ((hi.y - lo.y) * m->GetFileUnitScale() < 0.01f) continue;
                    out.push_back({ &path, m, m->GetFileUnitScale(), lo, hi });
                }
            return out;
        }

        // 1 個の石を「辺に沿う向き t に length、直交に depth、高さ height」の箱に収まるよう引き伸ばした結果。
        // 石の長い方の水平軸を t に合わせ（0 / 180° はランダム）、jitterDeg だけ余分に回す
        struct Fit
        {
            float yawDeg = 0.0f;
            Vector3 stretch = Vector3::One;   // 石のローカル軸ごとの倍率（unit とは別）
            Vector3 ax, az;                   // 回した後のローカル x / z 軸（世界）
            float hx = 0.0f, hz = 0.0f;       // 回した箱の半幅（ローカル軸方向）
            Vector3 mid;                      // 原点 → 箱の真ん中（世界、y は 0）
        };
        Fit FitRock(const Rock& r, const Vector3& t, float length, float depth, float height, bool flip, float jitterDeg)
        {
            const float dx = (r.hi.x - r.lo.x) * r.unit, dy = (r.hi.y - r.lo.y) * r.unit, dz = (r.hi.z - r.lo.z) * r.unit;
            const bool alongIsX = dx >= dz;
            Fit f;
            f.stretch.x = (alongIsX ? length : depth) / (std::max)(dx, 0.01f);
            f.stretch.z = (alongIsX ? depth : length) / (std::max)(dz, 0.01f);
            f.stretch.y = height / (std::max)(dy, 0.01f);
            // ローカルの長い軸が t に重なる向き：t が x 軸なら「長い軸 = x」で 0°、「長い軸 = z」で 90°。t が z 軸なら逆
            const bool tIsX = std::fabs(t.x) > std::fabs(t.z);
            f.yawDeg = ((tIsX == alongIsX) ? 0.0f : 90.0f) + (flip ? 180.0f : 0.0f) + jitterDeg;
            const auto rot = DirectX::SimpleMath::Matrix::CreateRotationY(DirectX::XMConvertToRadians(f.yawDeg));
            f.ax = Vector3::TransformNormal({ 1, 0, 0 }, rot);
            f.az = Vector3::TransformNormal({ 0, 0, 1 }, rot);
            f.hx = dx * f.stretch.x * 0.5f;
            f.hz = dz * f.stretch.z * 0.5f;
            f.mid = Vector3::TransformNormal(
                Vector3((r.hi.x + r.lo.x) * 0.5f * r.unit * f.stretch.x, 0.0f, (r.hi.z + r.lo.z) * 0.5f * r.unit * f.stretch.z), rot);
            return f;
        }
        // 箱の真ん中（xz）が center、底が base になるよう置く。collide なら凸包の衝突 + 足元のマスを塞ぐ（grid があれば）
        void Put(Emitter& emit, const Rock& r, const Fit& f, const Vector3& center, float base, MapData::Kind kind,
            bool collide = false, GridWorld* grid = nullptr, int* outBlocked = nullptr)
        {
            const Vector3 pos(center.x - f.mid.x, base - r.lo.y * r.unit * f.stretch.y, center.z - f.mid.z);
            emit.Begin(kind);
            emit.Prop(*r.path, r.model, pos, f.yawDeg, r.unit, f.stretch, collide);
            if (collide && grid)
            {
                const int n = BlockCellsUnderHull(emit, *grid, PropHullWorld(*r.model, pos, f.yawDeg, r.unit, f.stretch));
                if (outBlocked) *outBlocked += n;
            }
        }
    }

    // ============================================================
    // 外周の岩山：四辺それぞれ 2 列（手前 perSide 個、奥は半周期ずらして perSide + 1 個、奥ほど高く厚く）+ 四隅に 1 個。
    // 手前の列は箱の内側の面が縁から intrude だけ内（負 = 外）に来る所。底は縁の地面、高さは平原（山頂の脇は山頂）から
    // ============================================================
    int EmitRimBoulders(Emitter& emit, GridWorld* grid, float halfW, float halfD, const RimBoulderParams& P,
        const std::vector<std::string>& modelPaths, const EdgeSpanFn& edgeSpan, std::mt19937& rng, int* outBlocked)
    {
        if (outBlocked) *outBlocked = 0;
        const std::vector<Rock> rocks = LoadRocks(modelPaths);
        if (rocks.empty() || P.perSide <= 0) return 0;
        auto randi = [&](int a, int b) { return (b <= a) ? a : std::uniform_int_distribution<int>(a, b)(rng); };
        auto randf = [&](float a, float b) { return std::uniform_real_distribution<float>(a, (std::max)(a, b))(rng); };

        struct Side { Vector3 n, t; float edge, half; };
        const Side sides[] = {
            { { 0, 0, 1 }, { 1, 0, 0 }, halfD, halfW },
            { { 0, 0, -1 }, { 1, 0, 0 }, halfD, halfW },
            { { 1, 0, 0 }, { 0, 0, 1 }, halfW, halfD },
            { { -1, 0, 0 }, { 0, 0, 1 }, halfW, halfD },
        };
        // 一辺に perSide 個を 1 列：箱の真ん中が縁の線の上（+ centerOut）= 半分が場内に入る（2026-10-06、ユーザー：
        // 一方向 4〜5 個の超大きい石で山にし、中心を縁に置けば縁の空気壁は要らない）。隣とは 1/3 ほど重なる
        int placed = 0;
        for (const Side& sd : sides)
        {
            const float period = 2.0f * sd.half / (float)P.perSide;
            for (int i = 0; i < P.perSide; ++i)
            {
                const float s = -sd.half + (i + 0.5f) * period + randf(-period * 0.08f, period * 0.08f);
                const Rock& r = rocks[(size_t)randi(0, (int)rocks.size() - 1)];
                const float length = period * 1.35f * randf(0.92f, 1.08f);
                float lo = 0.0f, hi = 0.0f;
                edgeSpan(sd.n, sd.t, sd.edge, s, length * 0.4f, lo, hi);
                const float targetH = randf(P.frontHMin, P.frontHMax) * P.heightMul + ((std::max)(hi, 0.0f) - lo);
                const bool flip = randi(0, 1) != 0;
                const float jitter = randf(-P.yawJitterDeg, P.yawJitterDeg);
                const Fit f = FitRock(r, sd.t, length, P.depth * randf(0.9f, 1.1f), targetH, flip, jitter);
                const Vector3 center = sd.n * (sd.edge + P.centerOut) + sd.t * s;
                Put(emit, r, f, center, lo - targetH * P.sink, MapData::kEdgeRock, P.collide, grid, outBlocked);
                ++placed;
            }
        }

        // 石の後ろの岩色の壁（地形の合成モデルに入る。collide なら衝突も）：辺を 8 等分して、それぞれの縁の地面から backdropHeight。
        // 石の丸い端の間の隙間の突き当たりになる（空が見えない・外へ出られない）
        if (P.backdropHeight > 0.0f)
            for (const Side& sd : sides)
            {
                const float reach = P.depth * 0.5f;
                const int segs = 8;
                const float segLen = 2.0f * (sd.half + reach) / segs;
                for (int i = 0; i < segs; ++i)
                {
                    const float s0 = -sd.half - reach + i * segLen, s1 = s0 + segLen;
                    float lo = 0.0f, hi = 0.0f;
                    edgeSpan(sd.n, sd.t, sd.edge, (s0 + s1) * 0.5f, segLen * 0.5f, lo, hi);
                    const Vector3 a = sd.n * (sd.edge + P.backdropOut) + sd.t * s0;
                    const Vector3 b = sd.n * (sd.edge + P.backdropOut + 4.0f) + sd.t * s1;
                    const Vector3 bmin((std::min)(a.x, b.x), lo - 3.0f, (std::min)(a.z, b.z));
                    const Vector3 bmax((std::max)(a.x, b.x), (std::max)(hi, 0.0f) + P.backdropHeight, (std::max)(a.z, b.z));
                    Vector3 v[8];
                    BoxVerts(v, bmin, bmax);
                    emit.Begin(MapData::kEdgeRock);
                    emit.Visual(v, P.backdropColor, P.backdropColor, (int)TerrainSurface::Rock, (int)TerrainSurface::Rock);
                    // 石の衝突で止める時は、この壁にも衝突（石と石の間・石と山頂の崖の間の隙間から縁の外へ出て、
                    // 行き止まりの溝に嵌まった。2026-10-06 ユーザーの報告）
                    if (P.collide) emit.Box(bmin, bmax, Layer_Terrain);
                }
            }

        // 四隅：角の点の上に斜めに 1 個（二辺の列の継ぎ目を塞ぐ）。高さは backH
        for (int k = 0; k < 4; ++k)
        {
            const float sx = (k & 1) ? -1.0f : 1.0f, sz = (k & 2) ? -1.0f : 1.0f;
            const Rock& r = rocks[(size_t)randi(0, (int)rocks.size() - 1)];
            float lo = 0.0f, hi = 0.0f;
            edgeSpan({ sx, 0, 0 }, { 0, 0, 1 }, halfW, sz * halfD, 4.0f, lo, hi);
            const float targetH = randf(P.backHMin, P.backHMax) * P.heightMul + ((std::max)(hi, 0.0f) - lo);
            // 長い軸を x に合わせてから 45° 回す = 隅を斜めに横切る向き
            const Fit f = FitRock(r, Vector3(1, 0, 0), P.depth * 1.6f, P.depth * 1.2f, targetH, randi(0, 1) != 0,
                45.0f * sx * sz + randf(-P.yawJitterDeg, P.yawJitterDeg));
            const float c = P.centerOut * 0.7071f;
            const Vector3 center(sx * (halfW + c), 0.0f, sz * (halfD + c));
            Put(emit, r, f, center, lo - targetH * P.sink, MapData::kEdgeRock, P.collide, grid, outBlocked);
            ++placed;
        }
        return placed;
    }

    // ============================================================
    // 洞窟の上の山：岩の塊（坑 + 岩の壁）の包囲矩形に、真ん中の大きい 1 個 + 長い方の両端に 1 個ずつ。
    // 底は屋根の上面より下に埋める（縁が丸いので塊の縁は屋根に沈む）
    // ============================================================
    int EmitRoofBoulders(Emitter& emit, const GridWorld& origin, const std::vector<uint8_t>& zone,
        const std::vector<uint8_t>& ring, float roofTopY, float rockMin, float rockMax,
        const std::vector<std::string>& modelPaths, std::mt19937& rng)
    {
        const std::vector<Rock> rocks = LoadRocks(modelPaths);
        if (rocks.empty()) return 0;
        auto randi = [&](int a, int b) { return (b <= a) ? a : std::uniform_int_distribution<int>(a, b)(rng); };
        auto randf = [&](float a, float b) { return std::uniform_real_distribution<float>(a, (std::max)(a, b))(rng); };

        const int gw = origin.Width(), gd = origin.Depth();
        int x0 = gw, x1 = -1, z0 = gd, z1 = -1;
        for (int z = 0; z < gd; ++z)
            for (int x = 0; x < gw; ++x)
            {
                const size_t i = (size_t)z * gw + x;
                if (zone[i] != ReliefField::kMine && ring[i] == 0) continue;
                x0 = (std::min)(x0, x); x1 = (std::max)(x1, x);
                z0 = (std::min)(z0, z); z1 = (std::max)(z1, z);
            }
        if (x1 < x0) return 0;
        const Vector3 c = (origin.CellToWorld(x0, z0) + origin.CellToWorld(x1, z1)) * 0.5f;
        const float ex = (x1 - x0 + 1) * kCs * 0.5f, ez = (z1 - z0 + 1) * kCs * 0.5f;
        const bool alongX = ex >= ez;
        const Vector3 t = alongX ? Vector3(1, 0, 0) : Vector3(0, 0, 1);
        const float longE = (std::max)(ex, ez), shortE = (std::min)(ex, ez);

        int placed = 0;
        {
            // 真ん中：塊をほぼ覆う。高さは roofRockMax
            const Rock& r = rocks[(size_t)randi(0, (int)rocks.size() - 1)];
            const float h = rockMax * 1.4f * randf(0.95f, 1.05f);
            const Fit f = FitRock(r, t, longE * 2.0f * 1.08f, shortE * 2.0f * 1.08f, h, randi(0, 1) != 0, randf(-6.0f, 6.0f));
            Put(emit, r, f, c, roofTopY - h * 0.4f, MapData::kRoofRock);
            ++placed;
        }
        for (int k = -1; k <= 1; k += 2)
        {
            // 両端：低めの石を少し外へ（輪郭が 1 個の塊に見えないように）
            const Rock& r = rocks[(size_t)randi(0, (int)rocks.size() - 1)];
            const float h = rockMin * 1.3f * randf(0.9f, 1.1f);
            const Fit f = FitRock(r, t, longE * 1.1f, shortE * 1.5f, h, randi(0, 1) != 0, randf(-20.0f, 20.0f));
            const Vector3 center = c + t * (k * longE * 0.5f) + (alongX ? Vector3(0, 0, 1) : Vector3(1, 0, 0)) * randf(-shortE * 0.25f, shortE * 0.25f);
            Put(emit, r, f, center, roofTopY - h * 0.35f, MapData::kRoofRock);
            ++placed;
        }
        return placed;
    }
}
