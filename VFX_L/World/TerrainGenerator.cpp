// ============================================================
// TerrainGenerator.cpp
// ============================================================
#include "World/TerrainGenerator.h"
#include "World/GridWorld.h"
#include "ECS/Registry.h"
#include "Component/ModelComponent.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Debug/TestSpawner.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <random>
#include <cmath>
#include <iostream>

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

namespace
{
    constexpr float kCs = GridWorld::kCellSize;

    // ---- 色（明るい草地）----
    // 頂点色は線形の反照率（CompositePS が最後に 1/2.2 のガンマを掛ける）。
    // 画面で見せたい sRGB の色を 2.2 乗した値で持つ（括弧内が sRGB）
    const Vector4 kGrassDark = { 0.071f, 0.237f, 0.029f, 1 };   // (0.30, 0.52, 0.20)
    const Vector4 kGrassLight = { 0.172f, 0.428f, 0.052f, 1 };  // (0.45, 0.68, 0.26)
    const Vector4 kGrassDry = { 0.401f, 0.401f, 0.093f, 1 };    // (0.66, 0.66, 0.34) 所々の乾いた草
    const Vector4 kPlateauTop = { 0.133f, 0.374f, 0.047f, 1 };  // (0.40, 0.64, 0.25)
    const Vector4 kCliff = { 0.268f, 0.172f, 0.099f, 1 };       // (0.55, 0.45, 0.35) 台地の側面（土と岩）
    const Vector4 kCliffHigh = { 0.325f, 0.290f, 0.247f, 1 };   // (0.60, 0.57, 0.53) 2 段目は灰色がかった岩
    const Vector4 kRampTop = { 0.486f, 0.325f, 0.148f, 1 };     // (0.72, 0.60, 0.42) 坂道は土の道（登り口が一目で分かる）
    const Vector4 kWallRock = { 0.172f, 0.148f, 0.133f, 1 };    // (0.45, 0.42, 0.40)

    // 坂道が台地のどちら側に付くか（= 降りていく向き）
    enum class Side { PosX, NegX, PosZ, NegZ };

    struct Rect { int x, z, w, d; };   // 格子のマス（左下と大きさ）

    // 色を少しばらす（台地ごとに同じ緑にならないように）
    Vector4 Jitter(const Vector4& c, std::mt19937& rng, float amount)
    {
        std::uniform_real_distribution<float> j(-amount, amount);
        const float k = j(rng);
        return { std::clamp(c.x + k, 0.0f, 1.0f), std::clamp(c.y + k, 0.0f, 1.0f),
                 std::clamp(c.z + k * 0.5f, 0.0f, 1.0f), 1.0f };
    }

    Vector4 LerpColor(const Vector4& a, const Vector4& b, float t) { return a + (b - a) * t; }

    // ---- 値ノイズ（地面の色むら）。0..1 ----
    float Hash01(int x, int z, uint32_t seed)
    {
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)((h ^ (h >> 16)) & 0xFFFFFFu) / (float)0xFFFFFFu;
    }
    float ValueNoise(float x, float z, uint32_t seed)
    {
        const int ix = (int)std::floor(x), iz = (int)std::floor(z);
        float tx = x - ix, tz = z - iz;
        tx = tx * tx * (3.0f - 2.0f * tx);
        tz = tz * tz * (3.0f - 2.0f * tz);
        const float a = Hash01(ix, iz, seed), b = Hash01(ix + 1, iz, seed);
        const float c = Hash01(ix, iz + 1, seed), d = Hash01(ix + 1, iz + 1, seed);
        const float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
        return ab + (cd - ab) * tz;
    }

    // 格子の矩形の下隅（世界、y = 0）
    Vector3 RectMin(const GridWorld& g, const Rect& r)
    {
        return { g.OriginX() + r.x * kCs, 0.0f, g.OriginZ() + r.z * kCs };
    }

    // 軸平行の箱の 8 頂点。順は ConvexFromHexahedron / CreateHexahedron と同じ:
    // 下 0-3 = (-x-z, +x-z, +x+z, -x+z)、上 4-7 がそれぞれの真上
    void BoxVerts(Vector3 v[8], const Vector3& lo, const Vector3& hi)
    {
        v[0] = { lo.x, lo.y, lo.z }; v[1] = { hi.x, lo.y, lo.z };
        v[2] = { hi.x, lo.y, hi.z }; v[3] = { lo.x, lo.y, hi.z };
        v[4] = { lo.x, hi.y, lo.z }; v[5] = { hi.x, hi.y, lo.z };
        v[6] = { hi.x, hi.y, hi.z }; v[7] = { lo.x, hi.y, hi.z };
    }

    // 静的な凸体（世界座標の 8 頂点）。衝突は Convex の実体、
    // 見た目（上面と側面の 2 色）は batch へ積む（地形全部で 1 つのモデル。Generate の最後に作る）
    Entity SpawnHull(Registry& reg, PrimitiveBuilder::HexahedronBatch& batch, const Vector3 world[8],
        const Vector4& top, const Vector4& side)
    {
        Vector3 lo = world[0], hi = world[0];
        for (int i = 1; i < 8; ++i)
        {
            lo = Vector3::Min(lo, world[i]);
            hi = Vector3::Max(hi, world[i]);
        }
        const Vector3 center = (lo + hi) * 0.5f;
        Vector3 v[8];
        for (int i = 0; i < 8; ++i) v[i] = world[i] - center;

        Entity e = reg.Create();

        TransformComponent tf;
        tf.position = center;
        reg.Add<TransformComponent>(e, tf);

        ColliderComponent col;
        col.shape = ColliderShape::Convex;
        col.hull = CollisionMath::ConvexFromHexahedron(v);
        col.halfExtents = (hi - lo) * 0.5f;   // 広相位用の包囲箱
        col.layer = Layer_Terrain;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.isStatic = true;
        rb.useGravity = false;
        reg.Add<RigidbodyComponent>(e, rb);

        batch.Append(world, top, side);
        return e;
    }

    // 静的な箱（衝突は AABB の実体）。見た目（上面と側面の 2 色）は batch へ
    Entity SpawnBlock(Registry& reg, PrimitiveBuilder::HexahedronBatch& batch, const Vector3& lo, const Vector3& hi,
        const Vector4& top, const Vector4& side)
    {
        const Vector3 center = (lo + hi) * 0.5f;
        const Vector3 half = (hi - lo) * 0.5f;
        Entity e = TestSpawner::SpawnStaticBox(reg, center, half);

        Vector3 v[8];
        BoxVerts(v, lo, hi);
        batch.Append(v, top, side);
        return e;
    }

    // 足跡の高さ場を h まで上げる（台地の上面）
    void RaiseRect(GridWorld& g, const Rect& r, float h)
    {
        const int sub = GridWorld::kHeightSub;
        for (int hz = r.z * sub; hz < (r.z + r.d) * sub; ++hz)
            for (int hx = r.x * sub; hx < (r.x + r.w) * sub; ++hx)
                g.SetHeight(hx, hz, (std::max)(g.HeightAt(hx, hz), h));
    }

    // 凸体の上面を足跡の高さ場へ（坂道・高台の坂）。
    // 高さ = 上を向いた平面のうち一番低い物（下から見て初めて当たる面）
    void WriteHullHeights(GridWorld& g, const Rect& r, const CollisionMath::Convex& hull,
        const Vector3& center)
    {
        const int sub = GridWorld::kHeightSub;
        for (int hz = r.z * sub; hz < (r.z + r.d) * sub; ++hz)
            for (int hx = r.x * sub; hx < (r.x + r.w) * sub; ++hx)
            {
                const Vector3 p = g.HeightCellToWorld(hx, hz);
                const float lx = p.x - center.x;
                const float lz = p.z - center.z;
                float top = 1e9f;
                for (int i = 0; i < hull.count; ++i)
                {
                    const auto& pl = hull.planes[i];
                    if (pl.n.y <= 0.1f) continue;   // 上向きの面だけ
                    top = (std::min)(top, (pl.d - pl.n.x * lx - pl.n.z * lz) / pl.n.y);
                }
                if (top < 1e8f)
                    g.SetHeight(hx, hz, (std::max)(g.HeightAt(hx, hz), top + center.y));
            }
    }

    // ---- 坂道の足跡 ----
    // 台地 p の side 側の外に、辺に沿って offset マス目から幅 rw、長さ len
    Rect RampRect(const Rect& p, Side s, int offset, int rw, int len)
    {
        switch (s)
        {
        case Side::PosX: return { p.x + p.w, p.z + offset, len, rw };
        case Side::NegX: return { p.x - len, p.z + offset, len, rw };
        case Side::PosZ: return { p.x + offset, p.z + p.d, rw, len };
        default:         return { p.x + offset, p.z - len, rw, len };
        }
    }
    // 坂道を降りた先の 1 マス幅の帯。塞がれていると登り口にならない
    Rect LandingRect(const Rect& ramp, Side s)
    {
        switch (s)
        {
        case Side::PosX: return { ramp.x + ramp.w, ramp.z, 1, ramp.d };
        case Side::NegX: return { ramp.x - 1, ramp.z, 1, ramp.d };
        case Side::PosZ: return { ramp.x, ramp.z + ramp.d, ramp.w, 1 };
        default:         return { ramp.x, ramp.z - 1, ramp.w, 1 };
        }
    }
    int SideLength(const Rect& p, Side s)
    {
        return (s == Side::PosX || s == Side::NegX) ? p.d : p.w;
    }

    // 台地 p の中の矩形を「坂の側から」測って取る。
    // u = side s の辺から内側への奥行き（u0 から ulen マス）、v = その辺に沿った位置（v0 から vlen マス）
    Rect LocalRect(const Rect& p, Side s, int u0, int ulen, int v0, int vlen)
    {
        switch (s)
        {
        case Side::PosX: return { p.x + p.w - u0 - ulen, p.z + v0, ulen, vlen };
        case Side::NegX: return { p.x + u0, p.z + v0, ulen, vlen };
        case Side::PosZ: return { p.x + v0, p.z + p.d - u0 - ulen, vlen, ulen };
        default:         return { p.x + v0, p.z + u0, vlen, ulen };
        }
    }

    // 坂道の楔。高い端（top）が台地の側面に接し、外へ向かって base まで下る
    Entity SpawnRamp(Registry& reg, PrimitiveBuilder::HexahedronBatch& batch, GridWorld& g, const Rect& r, Side s,
        float base, float top, const Vector4& topColor = kRampTop)
    {
        const Vector3 lo = RectMin(g, r);
        const Vector3 hi = lo + Vector3(r.w * kCs, 0.0f, r.d * kCs);
        const float low = base + 0.02f;   // 低い端にも厚みを残す（面が潰れて平面が作れなくならない）

        Vector3 v[8];
        BoxVerts(v, { lo.x, base, lo.z }, { hi.x, top, hi.z });
        // 上面 4-7 のうち台地から遠い側を下げる（-x = 4,7 / +x = 5,6 / -z = 4,5 / +z = 6,7）
        switch (s)
        {
        case Side::PosX: v[5].y = v[6].y = low; break;
        case Side::NegX: v[4].y = v[7].y = low; break;
        case Side::PosZ: v[6].y = v[7].y = low; break;
        case Side::NegZ: v[4].y = v[5].y = low; break;
        }

        Entity e = SpawnHull(reg, batch, v, topColor, kCliff);
        WriteHullHeights(g, r, reg.Get<ColliderComponent>(e).hull, reg.Get<TransformComponent>(e).position);
        return e;
    }
}

namespace TerrainGenerator
{
    DirectX::SimpleMath::Vector4 GroundColor(float x, float z, uint32_t seed)
    {
        const float big = ValueNoise(x / 22.0f, z / 22.0f, seed);
        const float fine = ValueNoise(x / 6.0f, z / 6.0f, seed + 7u);
        const Vector4 c = LerpColor(kGrassDark, kGrassLight,
            std::clamp(big * 0.8f + fine * 0.4f - 0.1f, 0.0f, 1.0f));
        const float dry = std::clamp((ValueNoise(x / 35.0f, z / 35.0f, seed + 13u) - 0.62f) * 4.0f, 0.0f, 0.6f);
        return LerpColor(c, kGrassDry, dry);
    }

    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask)
    {
        const int gw = grid.Width();
        const int gd = grid.Depth();
        const float W = grid.WorldWidth();
        const float D = grid.WorldDepth();

        // seed 固定の mt19937。rand() を使わないのは再現性のため
        std::mt19937 rng(cfg.seed);
        auto randi = [&](int a, int b) { return (b <= a) ? a : std::uniform_int_distribution<int>(a, b)(rng); };
        auto randf = [&](float a, float b) { return std::uniform_real_distribution<float>(a, (std::max)(a, b))(rng); };

        // 外周の崖・台地・坂道・高台の見た目は全部ここへ積み、最後に 1 つのモデルにする
        // （衝突は 1 個ずつの実体のまま。見た目だけ 1 回の draw）
        PrimitiveBuilder::HexahedronBatch batch;

        // ---------- 床（草地）----------
        // 衝突は上面が y=0 の箱、見た目は 2m 間隔の格子に値ノイズで緑のむらと乾いた草を塗った面。
        // 床は格子に登記しない（上を歩くものなので通行を塞がない）
        {
            Entity e = TestSpawner::SpawnStaticBox(reg, { 0.0f, -0.5f, 0.0f }, { W * 0.5f, 0.5f, D * 0.5f });
            const uint32_t s = cfg.seed;
            ModelComponent mc;
            mc.model = PrimitiveBuilder::CreateColoredGrid(device, W, D, gw, gd, 0.5f,
                [s](float x, float z) { return GroundColor(x, z, s); });
            reg.Add<ModelComponent>(e, mc);
            outTerrain.push_back(e);
        }

        // ---------- 外周の崖 ----------
        // 1 マス幅で四辺を囲む。場外へ出る・落ちるをここで殺す（格子も塞ぐ）
        auto wall = [&](int x, int z, int w, int d)
            {
                const Vector3 lo = RectMin(grid, { x, z, w, d });
                const Vector3 hi = lo + Vector3(w * kCs, cfg.wallHeight, d * kCs);
                outTerrain.push_back(SpawnBlock(reg, batch, lo, hi, kPlateauTop, kWallRock));
                grid.BlockArea(x, z, w, d);
            };
        wall(0, 0, gw, 1);
        wall(0, gd - 1, gw, 1);
        wall(0, 1, 1, gd - 2);
        wall(gw - 1, 1, 1, gd - 2);

        // ---------- 占有図 ----------
        // 空き地 / 台地・高台の上面 / 坂道 / 初期地点 / 坂道の降り口（空き地のまま残す）/ 高台の長い坂 /
        // 高台の坂の麓（地面のまま。木・岩・台地は置かない。茂み・草は置く）/ 高台の上の通り道（木・岩を置かない）
        enum : uint8_t { kFree, kPlateau, kRamp, kSpawn, kLanding, kSlope, kApron, kWay };
        std::vector<uint8_t> occ((size_t)gw * gd, kFree);
        auto at = [&](int x, int z) -> uint8_t& { return occ[(size_t)z * gw + x]; };
        auto inside = [&](const Rect& r)   // 外周の崖と、その内側 1 マスは使わない
            { return r.x >= 2 && r.z >= 2 && r.x + r.w <= gw - 2 && r.z + r.d <= gd - 2; };
        auto isFree = [&](const Rect& r, int margin)
            {
                for (int z = r.z - margin; z < r.z + r.d + margin; ++z)
                    for (int x = r.x - margin; x < r.x + r.w + margin; ++x)
                        if (x >= 0 && z >= 0 && x < gw && z < gd && at(x, z) != kFree) return false;
                return true;
            };
        auto isGround = [&](const Rect& r)   // 降り口：地面のまま（空き地・初期地点・他の降り口）
            {
                for (int z = r.z; z < r.z + r.d; ++z)
                    for (int x = r.x; x < r.x + r.w; ++x)
                    {
                        const uint8_t o = at(x, z);
                        if (o != kFree && o != kSpawn && o != kLanding && o != kApron) return false;
                    }
                return true;
            };
        auto mark = [&](const Rect& r, uint8_t v)
            {
                for (int z = (std::max)(r.z, 0); z < (std::min)(r.z + r.d, gd); ++z)
                    for (int x = (std::max)(r.x, 0); x < (std::min)(r.x + r.w, gw); ++x)
                        at(x, z) = v;
            };

        // 玩家の初期地点（場地中央）の周りは平らに空ける
        const int cr = cfg.spawnClearRadius;
        mark({ gw / 2 - cr, gd / 2 - cr, cr * 2, cr * 2 }, kSpawn);

        const float tanRamp = std::tan(DirectX::XMConvertToRadians(cfg.rampSlopeDeg));
        auto rampLen = [&](float rise) { return (std::max)(1, (int)std::ceil(rise / (kCs * tanRamp))); };

        // ---------- 高台（一面だけが長い坂、残り三面は崖。一部は上に 2 段目）----------
        // 場所を大きく取るので台地より先に置く。坂の向きは高台ごとにランダム。
        //   1 段目: 上面 p（高さ h1）+ その辺いっぱいの幅の坂（長さ = h1 / tan 角度）
        //   2 段目: p の奥に上面（+h2）と、同じ向きの坂。p の坂側 2 マスは 1 段目のまま残す通り道、
        //           2 段目の坂以外の周りは 1 マスの縁を残す → 地面・1 段目・2 段目の 3 段が見える
        //   通り道（kWay）には木・岩を置かない（2 段目 → 1 段目 → 地面と滑り継げる）。
        //   坂の麓の先 terraceClear マスは滑り出す空き地（kApron）。場外の壁ともこれだけ空ける
        const int clear = (std::max)(cfg.terraceClear, 1);
        int terraces = 0, terraceTier2 = 0;
        for (int attempt = 0; attempt < cfg.terraceCount * 40 && terraces < cfg.terraceCount; ++attempt)
        {
            const float tanS = std::tan(DirectX::XMConvertToRadians(
                std::clamp(randf(cfg.terraceSlopeMin, cfg.terraceSlopeMax), 5.0f, 35.0f)));
            auto slopeLen = [&](float rise) { return (std::max)(1, (int)std::ceil(rise / tanS / kCs)); };
            const float h1 = randf(cfg.terraceHeightMin, cfg.terraceHeightMax);
            const int run1 = slopeLen(h1);
            const int across = randi(cfg.terraceTopMin, cfg.terraceTopMax);
            const bool tier2 = across >= 5 && randf(0.0f, 1.0f) < cfg.terraceTier2Chance;
            const float h2 = tier2 ? randf(cfg.terraceTier2HeightMin, cfg.terraceTier2HeightMax) : 0.0f;
            const int run2 = tier2 ? slopeLen(h2) : 0;
            const int top2 = tier2 ? randi(3, 5) : 0;
            // 奥行き：2 段目があれば 通り道 2 + 坂 + 上面 + 奥の縁 1
            const int along = tier2 ? 2 + run2 + top2 + 1 : randi(cfg.terraceTopMin, cfg.terraceTopMax);
            const Side s = (Side)randi(0, 3);
            const bool alongX = (s == Side::PosX || s == Side::NegX);

            Rect p = { 0, 0, alongX ? along : across, alongX ? across : along };
            if (p.w > gw - 4 || p.d > gd - 4) continue;
            p.x = randi(2, gw - 2 - p.w);
            p.z = randi(2, gd - 2 - p.d);
            const Rect sr = RampRect(p, s, 0, across, run1);
            const Rect apron = RampRect(sr, s, -1, across + 2, clear);   // 坂の幅 + 左右 1 マス
            if (!inside(p) || !inside(sr)) continue;
            if (apron.x < 1 || apron.z < 1 || apron.x + apron.w > gw - 1 || apron.z + apron.d > gd - 1) continue;
            // 坂は場外の壁に向けない：麓から下る向きに壁まで 12 マス（24m。20 m/s で滑り出しても曲がる余裕）
            constexpr int kWallRunout = 12;
            int toWall = 0;
            switch (s)
            {
            case Side::PosX: toWall = (gw - 1) - (sr.x + sr.w); break;
            case Side::NegX: toWall = sr.x - 1; break;
            case Side::PosZ: toWall = (gd - 1) - (sr.z + sr.d); break;
            case Side::NegZ: toWall = sr.z - 1; break;
            }
            if (toWall < (std::max)(kWallRunout, clear)) continue;
            if (!isFree(p, 2) || !isFree(sr, 2)) continue;   // 他の高台・その麓・初期地点から 2 マス
            // 麓は他の麓・初期地点とは重なってよい（高台の体に被らなければ）
            bool apronOk = true;
            for (int z = apron.z; z < apron.z + apron.d && apronOk; ++z)
                for (int x = apron.x; x < apron.x + apron.w && apronOk; ++x)
                    apronOk = (at(x, z) == kFree || at(x, z) == kApron || at(x, z) == kSpawn);
            if (!apronOk) continue;

            for (int z = apron.z; z < apron.z + apron.d; ++z)
                for (int x = apron.x; x < apron.x + apron.w; ++x)
                    if (at(x, z) == kFree) at(x, z) = kApron;
            mark(p, kPlateau);
            mark(sr, kSlope);
            mark(LocalRect(p, s, 0, 2, 0, across), kWay);

            const Vector3 lo = RectMin(grid, p);
            outTerrain.push_back(SpawnBlock(reg, batch, lo, lo + Vector3(p.w * kCs, h1, p.d * kCs),
                Jitter(kPlateauTop, rng, 0.02f), Jitter(kCliff, rng, 0.015f)));
            RaiseRect(grid, p, h1);
            outTerrain.push_back(SpawnRamp(reg, batch, grid, sr, s, 0.0f, h1, Jitter(kGrassLight, rng, 0.02f)));

            if (tier2)
            {
                const int w2 = randi(3, across - 2);
                const int v0 = randi(1, across - 1 - w2);
                const Rect t = LocalRect(p, s, 2 + run2, top2, v0, w2);
                const Rect s2 = LocalRect(p, s, 2, run2, v0, w2);
                const Vector3 tlo = RectMin(grid, t);
                outTerrain.push_back(SpawnBlock(reg, batch, tlo + Vector3(0.0f, h1, 0.0f),
                    tlo + Vector3(t.w * kCs, h1 + h2, t.d * kCs),
                    Jitter(kPlateauTop, rng, 0.02f), Jitter(kCliffHigh, rng, 0.015f)));
                RaiseRect(grid, t, h1 + h2);
                outTerrain.push_back(SpawnRamp(reg, batch, grid, s2, s, h1, h1 + h2, Jitter(kGrassLight, rng, 0.02f)));
                mark(s2, kSlope);
                ++terraceTier2;
            }
            ++terraces;
        }

        // ---------- 1 段目の台地（場所だけ先に決める）----------
        struct Plateau { Rect r; float top; bool reachable; };
        std::vector<Plateau> plateaus;
        for (int attempt = 0; attempt < cfg.plateauCount * 30 && (int)plateaus.size() < cfg.plateauCount; ++attempt)
        {
            Rect r = { 0, 0, randi(cfg.plateauMin, cfg.plateauMax), randi(cfg.plateauMin, cfg.plateauMax) };
            r.x = randi(2, gw - 2 - r.w);
            r.z = randi(2, gd - 2 - r.d);
            // 間隔 gap を空ける = 台地同士の間に地上の通路が残る
            if (!inside(r) || !isFree(r, cfg.gap)) continue;
            mark(r, kPlateau);
            plateaus.push_back({ r, randf(cfg.heightMin, cfg.heightMax), false });
        }

        // ---------- 坂道（1 段目）。1〜2 本、降り口が地面に続く所だけ ----------
        auto placeRamp = [&](const Rect& p, Side s, float base, float top, bool onGround) -> bool
            {
                const int sideLen = SideLength(p, s);
                if (sideLen < cfg.rampWidth) return false;
                const int len = rampLen(top - base);
                const Rect rr = RampRect(p, s, randi(0, sideLen - cfg.rampWidth), cfg.rampWidth, len);
                const Rect land = LandingRect(rr, s);
                if (onGround && (!inside(rr) || !isFree(rr, 0) || !inside(land) || !isGround(land)))
                    return false;
                // 2 段目の坂道（1 段目の上）も載せる：置物で登り口を塞がないように
                mark(rr, kRamp);
                mark(land, kLanding);
                outTerrain.push_back(SpawnRamp(reg, batch, grid, rr, s, base, top));
                return true;
            };

        int rampCount = 0;
        int blocked = 0;
        for (auto& p : plateaus)
        {
            Side order[4] = { Side::PosX, Side::NegX, Side::PosZ, Side::NegZ };
            std::shuffle(order, order + 4, rng);
            const int want = (randf(0.0f, 1.0f) < 0.5f) ? 2 : 1;
            int made = 0;
            for (Side s : order)
                if (made < want && placeRamp(p.r, s, 0.0f, p.top, true)) ++made;
            p.reachable = (made > 0);
            rampCount += made;
        }

        // ---------- 台地の本体 ----------
        // 坂道が 1 本も付かなかった台地は登れない：格子を塞いで上に何も湧かないようにする
        for (const auto& p : plateaus)
        {
            const Vector3 lo = RectMin(grid, p.r);
            const Vector3 hi = lo + Vector3(p.r.w * kCs, p.top, p.r.d * kCs);
            outTerrain.push_back(SpawnBlock(reg, batch, lo, hi,
                Jitter(kPlateauTop, rng, 0.02f), Jitter(kCliff, rng, 0.015f)));
            if (p.reachable) RaiseRect(grid, p.r, p.top);
            else { grid.BlockArea(p.r.x, p.r.z, p.r.w, p.r.d); ++blocked; }
        }

        // ---------- 2 段目（大きい台地の上に小さい台地 + 1 段目の上から登る坂道）----------
        // 1 段目の縁 1 マスは空けて残す（1 段目の坂道の着き口と、上の周回路になる）。
        // 2 段目の坂道の側は、坂道の長さ + 降り口 1 マスも空ける
        int tier2 = 0;
        for (const auto& p : plateaus)
        {
            if (!p.reachable || p.r.w < 8 || p.r.d < 8) continue;
            if (randf(0.0f, 1.0f) >= cfg.tier2Chance) continue;

            const float rise = randf(cfg.tier2HeightMin, cfg.tier2HeightMax);
            const int len = rampLen(rise);
            const Side s = (Side)randi(0, 3);

            int x0 = p.r.x + 1, x1 = p.r.x + p.r.w - 1;   // [x0, x1)
            int z0 = p.r.z + 1, z1 = p.r.z + p.r.d - 1;
            switch (s)
            {
            case Side::PosX: x1 -= len + 1; break;
            case Side::NegX: x0 += len + 1; break;
            case Side::PosZ: z1 -= len + 1; break;
            case Side::NegZ: z0 += len + 1; break;
            }
            if (x1 - x0 < 3 || z1 - z0 < 3) continue;

            Rect t = { 0, 0, randi(3, (std::min)(x1 - x0, 6)), randi(3, (std::min)(z1 - z0, 6)) };
            t.x = randi(x0, x1 - t.w);
            t.z = randi(z0, z1 - t.d);

            const float top = p.top + rise;
            const Vector3 lo = RectMin(grid, t) + Vector3(0.0f, p.top, 0.0f);
            const Vector3 hi = RectMin(grid, t) + Vector3(t.w * kCs, top, t.d * kCs);
            outTerrain.push_back(SpawnBlock(reg, batch, lo, hi,
                Jitter(kPlateauTop, rng, 0.02f), Jitter(kCliffHigh, rng, 0.015f)));
            RaiseRect(grid, t, top);
            placeRamp(t, s, p.top, top, false);
            ++rampCount;
            ++tier2;
        }

        // ---------- 地形の見た目（外周・台地・坂道・高台）を 1 つのモデルに ----------
        // 1 個ずつだと DrawMesh が数十回（影の 3 段でその 3 倍）。頂点は世界座標なので実体は原点
        if (auto model = batch.Build(device))
        {
            Entity e = reg.Create();
            reg.Add<TransformComponent>(e, TransformComponent{});
            ModelComponent mc;
            mc.model = model;
            reg.Add<ModelComponent>(e, mc);
            outTerrain.push_back(e);
        }

        // ---------- 自然物（KayKit Forest）----------
        // 木と岩（置物）: 足跡のマス + 周り 1 マスが「歩ける・同じ高さ・空き地か台地の上・他の置物無し」の所だけ。
        //   坂道・降り口・台地の縁（周りの高さが違う）には置かないので、登り口は塞がれない。
        //   足跡は格子で塞ぐ（雑魚は避けて通る。GPU の弾もそのマスで当たる）。
        //   衝突は別の実体の箱（Layer_Prop：玩家はぶつかるが、カメラの遮蔽判定は見ない）。
        // 茂みと草: 見た目だけ。坂道の上以外ならどこでも（初期地点にも生える）
        struct PropModel { std::shared_ptr<Model> model; float unit; Vector3 lo, hi; };
        auto loadModels = [](const char* const* paths, size_t n)
            {
                std::vector<PropModel> out;
                for (size_t i = 0; i < n; ++i)
                {
                    auto m = ResourceManager::Get().LoadModel(paths[i]);
                    if (m) out.push_back({ m, m->GetFileUnitScale(), m->GetBoundsMin(), m->GetBoundsMax() });
                }
                return out;
            };
        namespace F = Res::Mdl::Forest;
        const auto trees = loadModels(F::kTrees, std::size(F::kTrees));
        const auto bareTrees = loadModels(F::kBareTrees, std::size(F::kBareTrees));
        const auto rocks = loadModels(F::kRocks, std::size(F::kRocks));
        const auto bushes = loadModels(F::kBushes, std::size(F::kBushes));

        auto cellHeight = [&](int x, int z) { const Vector3 c = grid.CellToWorld(x, z); return grid.SampleHeight(c.x, c.z); };
        std::vector<uint8_t> propAt((size_t)gw * gd, 0);   // 置物で塞いだマス

        // 見た目の実体（底を地面に合わせ、y 軸だけ回す）
        auto spawnVisual = [&](const PropModel& pm, float scale, float yawDeg, float x, float z, float ground)
            {
                const float u = pm.unit * scale;
                Entity e = reg.Create();
                TransformComponent tf;
                tf.position = { x, ground - pm.lo.y * u, z };
                tf.rotation = { 0.0f, yawDeg, 0.0f };
                tf.scale = { u, u, u };
                reg.Add<TransformComponent>(e, tf);
                ModelComponent mc;
                mc.model = pm.model;
                mc.batched = true;   // 場面の StaticPropRenderer がまとめて描く
                reg.Add<ModelComponent>(e, mc);
                outTerrain.push_back(e);
            };
        // 衝突だけの実体（軸平行の箱）
        auto spawnPropCollider = [&](const Vector3& center, const Vector3& half)
            {
                Entity e = TestSpawner::SpawnStaticBox(reg, center, half);
                reg.Get<ColliderComponent>(e).layer = Layer_Prop;
                outTerrain.push_back(e);
            };

        // 置物 1 個。回した包囲箱の足跡（木は幹のある 1 マスだけ）を調べて置く
        auto placeBlocker = [&](const PropModel& pm, float scale, float yawDeg, float x, float z, bool isTree) -> bool
            {
                const float u = pm.unit * scale;
                const float yaw = DirectX::XMConvertToRadians(yawDeg);
                const float cs = std::fabs(std::cos(yaw)), sn = std::fabs(std::sin(yaw));
                const float ex = (pm.hi.x - pm.lo.x) * 0.5f * u, ez = (pm.hi.z - pm.lo.z) * 0.5f * u;
                const float hx = cs * ex + sn * ez, hz = sn * ex + cs * ez;   // 回した後の半幅

                int gx0, gz0, gx1, gz1;
                if (isTree)
                {
                    grid.WorldToCell({ x, 0.0f, z }, gx0, gz0);
                    gx1 = gx0; gz1 = gz0;
                }
                else
                {
                    grid.WorldToCell({ x - hx, 0.0f, z - hz }, gx0, gz0);
                    grid.WorldToCell({ x + hx, 0.0f, z + hz }, gx1, gz1);
                }
                int cx0, cz0;
                grid.WorldToCell({ x, 0.0f, z }, cx0, cz0);
                if (cx0 < 1 || cz0 < 1 || cx0 >= gw - 1 || cz0 >= gd - 1) return false;
                const float h = cellHeight(cx0, cz0);
                for (int zz = gz0 - 1; zz <= gz1 + 1; ++zz)
                    for (int xx = gx0 - 1; xx <= gx1 + 1; ++xx)
                    {
                        if (xx < 1 || zz < 1 || xx >= gw - 1 || zz >= gd - 1) return false;
                        const uint8_t o = at(xx, zz);
                        if ((o != kFree && o != kPlateau) || !grid.IsWalkable(xx, zz) || propAt[(size_t)zz * gw + xx]) return false;
                        if (std::fabs(cellHeight(xx, zz) - h) > 0.2f) return false;
                    }

                for (int zz = gz0; zz <= gz1; ++zz)
                    for (int xx = gx0; xx <= gx1; ++xx)
                        propAt[(size_t)zz * gw + xx] = 1;
                grid.BlockArea(gx0, gz0, gx1 - gx0 + 1, gz1 - gz0 + 1);

                spawnVisual(pm, scale, yawDeg, x, z, h);
                const float height = (pm.hi.y - pm.lo.y) * u;
                if (isTree)
                {
                    const float trunk = (std::min)(height, 3.0f);
                    spawnPropCollider({ x, h + trunk * 0.5f, z }, { 0.3f, trunk * 0.5f, 0.3f });
                }
                else
                    spawnPropCollider({ x, h + height * 0.5f, z }, { hx * 0.85f, height * 0.5f, hz * 0.85f });
                return true;
            };

        auto randomPoint = [&](float& x, float& z)
            {
                x = grid.OriginX() + randf(2.0f * kCs, (gw - 2) * kCs);
                z = grid.OriginZ() + randf(2.0f * kCs, (gd - 2) * kCs);
            };

        // 木：林（大きいノイズの高い所）に固まり、所々に 1 本。枯れ木を 1 割
        int treesPlaced = 0;
        for (int attempt = 0; attempt < cfg.treeCount * 40 && treesPlaced < cfg.treeCount && !trees.empty(); ++attempt)
        {
            float x, z;
            randomPoint(x, z);
            const bool grove = ValueNoise(x / 26.0f, z / 26.0f, cfg.seed + 21u) > 0.58f;
            if (!grove && randf(0.0f, 1.0f) > 0.08f) continue;
            const bool bare = !bareTrees.empty() && randf(0.0f, 1.0f) < 0.1f;
            const auto& list = bare ? bareTrees : trees;
            const PropModel& pm = list[randi(0, (int)list.size() - 1)];
            if (placeBlocker(pm, randf(0.9f, 1.3f), randf(0.0f, 360.0f), x, z, true)) ++treesPlaced;
        }

        int rocksPlaced = 0;
        for (int attempt = 0; attempt < cfg.rockCount * 40 && rocksPlaced < cfg.rockCount && !rocks.empty(); ++attempt)
        {
            float x, z;
            randomPoint(x, z);
            const PropModel& pm = rocks[randi(0, (int)rocks.size() - 1)];
            if (placeBlocker(pm, randf(0.8f, 1.5f), randf(0.0f, 360.0f), x, z, false)) ++rocksPlaced;
        }

        // 茂み：平らな所（坂道・坂の上以外）。半分を林の中へ。
        // 草は模型ではなく GrassRenderer が GPU で生やす（下の outGrassMask）
        int bushesPlaced = 0;
        for (int attempt = 0; attempt < cfg.bushCount * 10 && bushesPlaced < cfg.bushCount && !bushes.empty(); ++attempt)
        {
            float x, z;
            randomPoint(x, z);
            if (randf(0.0f, 1.0f) < 0.5f && ValueNoise(x / 26.0f, z / 26.0f, cfg.seed + 21u) <= 0.58f) continue;
            int gx, gz;
            grid.WorldToCell({ x, 0.0f, z }, gx, gz);
            if (!grid.IsWalkable(gx, gz) || at(gx, gz) == kRamp) continue;
            const float h = grid.SampleHeight(x, z);
            if (std::fabs(grid.SampleHeight(x + 0.6f, z) - h) > 0.1f
                || std::fabs(grid.SampleHeight(x, z + 0.6f) - h) > 0.1f
                || std::fabs(grid.SampleHeight(x - 0.6f, z) - h) > 0.1f
                || std::fabs(grid.SampleHeight(x, z - 0.6f) - h) > 0.1f) continue;
            spawnVisual(bushes[randi(0, (int)bushes.size() - 1)], randf(0.8f, 1.3f), randf(0.0f, 360.0f), x, z, h);
            ++bushesPlaced;
        }

        // ---------- 草を生やすマス ----------
        // 土の坂道・外周の崖・登れない台地（高さ場が 0 のまま = 箱の中に生えてしまう）以外。
        // 崖の面そのものは GrassRenderer が高さ場の傾きで弾く
        if (outGrassMask)
        {
            auto& mask = *outGrassMask;
            mask.assign((size_t)gw * gd, 1);
            for (int z = 0; z < gd; ++z)
                for (int x = 0; x < gw; ++x)
                    if (x == 0 || z == 0 || x == gw - 1 || z == gd - 1 || at(x, z) == kRamp)
                        mask[(size_t)z * gw + x] = 0;
            for (const auto& p : plateaus)
                if (!p.reachable)
                    for (int z = p.r.z; z < p.r.z + p.r.d; ++z)
                        for (int x = p.r.x; x < p.r.x + p.r.w; ++x)
                            mask[(size_t)z * gw + x] = 0;
        }

        std::cout << "[Terrain] seed " << cfg.seed << ": " << terraces << " terraces (" << terraceTier2
            << " with a 2nd tier), " << plateaus.size() << " plateaus ("
            << tier2 << " with a 2nd tier, " << blocked << " without a ramp), "
            << rampCount << " ramps, " << treesPlaced << " trees, "
            << rocksPlaced << " rocks, " << bushesPlaced << " bushes, grid "
            << gw << "x" << gd << std::endl;
    }
}
