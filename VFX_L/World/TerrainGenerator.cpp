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
#include <iomanip>
#include <iostream>
#include <string>

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

namespace
{
    constexpr float kCs = GridWorld::kCellSize;

    // ---- 色（明るい草地）----
    // 頂点色は線形の反照率（CompositePS が最後に 1/2.2 のガンマを掛ける）。
    // 画面で見せたい sRGB の色を 2.2 乗した値で持つ（括弧内が sRGB）
    // 面（Biome）ごとの色の組。Generate の頭で g* へ写す（helper が既定引数で参照するので変数にしてある）
    struct Palette
    {
        Vector4 groundDark, groundLight, groundDry;   // 床：暗い / 明るい / 所々の別色（乾いた草・砂利・苔）
        Vector4 plateauTop, cliff, cliffHigh;         // 台地の上面 / 側面 / 2 段目の側面
        Vector4 rampTop, wallRock;                    // 坂道の上面 / 外周の箱（岩山でない時）
        Vector4 mineDark, mineLight;                  // 鉱洞の底：暗い / 明るい（土と砂利）
    };
    const Palette kPaletteGrass = {
        { 0.071f, 0.237f, 0.029f, 1 },   // (0.30, 0.52, 0.20)
        { 0.172f, 0.428f, 0.052f, 1 },   // (0.45, 0.68, 0.26)
        { 0.401f, 0.401f, 0.093f, 1 },   // (0.66, 0.66, 0.34) 所々の乾いた草
        { 0.133f, 0.374f, 0.047f, 1 },   // (0.40, 0.64, 0.25)
        { 0.268f, 0.172f, 0.099f, 1 },   // (0.55, 0.45, 0.35) 台地の側面（土と岩）
        { 0.325f, 0.290f, 0.247f, 1 },   // (0.60, 0.57, 0.53) 2 段目は灰色がかった岩
        { 0.486f, 0.325f, 0.148f, 1 },   // (0.72, 0.60, 0.42) 坂道は土の道（登り口が一目で分かる）
        { 0.172f, 0.148f, 0.133f, 1 },   // (0.45, 0.42, 0.40)
        { 0.106f, 0.076f, 0.052f, 1 },   // (0.36, 0.31, 0.26) 鉱洞の底（湿った土）
        { 0.218f, 0.173f, 0.119f, 1 },   // (0.50, 0.45, 0.38)
    };
    const Palette kPaletteDesert = {     // 砂（2026-09-30）
        { 0.480f, 0.300f, 0.120f, 1 },   // (0.72, 0.58, 0.38) 砂丘の影
        { 0.710f, 0.510f, 0.240f, 1 },   // (0.86, 0.74, 0.52) 明るい砂
        { 0.320f, 0.220f, 0.110f, 1 },   // (0.60, 0.50, 0.36) 砂利
        { 0.580f, 0.350f, 0.150f, 1 },   // (0.78, 0.62, 0.42) 砂岩の上面
        { 0.350f, 0.160f, 0.060f, 1 },   // (0.62, 0.44, 0.28) 赤い岩壁
        { 0.460f, 0.270f, 0.130f, 1 },   // (0.70, 0.55, 0.40)
        { 0.460f, 0.280f, 0.130f, 1 },   // (0.70, 0.56, 0.40) 坂道
        { 0.270f, 0.150f, 0.070f, 1 },   // (0.55, 0.42, 0.30)
        { 0.268f, 0.119f, 0.043f, 1 },   // (0.55, 0.38, 0.24) 鉱洞の底（赤い砂岩）
        { 0.428f, 0.218f, 0.082f, 1 },   // (0.68, 0.50, 0.32)
    };
    const Palette kPaletteDungeon = {    // 石畳（2026-09-30）
        { 0.062f, 0.058f, 0.056f, 1 },   // (0.28, 0.27, 0.27) 暗い石
        { 0.240f, 0.228f, 0.215f, 1 },   // (0.52, 0.51, 0.50) 明るい石
        { 0.071f, 0.106f, 0.052f, 1 },   // (0.30, 0.36, 0.26) 苔
        { 0.133f, 0.133f, 0.149f, 1 },   // (0.40, 0.40, 0.42) 段の上面
        { 0.061f, 0.056f, 0.066f, 1 },   // (0.28, 0.27, 0.29) 段の側面
        { 0.093f, 0.087f, 0.099f, 1 },   // (0.34, 0.33, 0.35)
        { 0.180f, 0.165f, 0.133f, 1 },   // (0.46, 0.44, 0.40) 坂道（砂岩の板）
        { 0.047f, 0.047f, 0.056f, 1 },   // (0.25, 0.25, 0.27)
        { 0.036f, 0.032f, 0.036f, 1 },   // (0.22, 0.21, 0.22) 鉱洞の底（暗い岩）
        { 0.106f, 0.093f, 0.087f, 1 },   // (0.36, 0.34, 0.33)
    };
    const Palette& PaletteFor(TerrainGenerator::Biome b)
    {
        switch (b)
        {
        case TerrainGenerator::Biome::Desert:  return kPaletteDesert;
        case TerrainGenerator::Biome::Dungeon: return kPaletteDungeon;
        default:                               return kPaletteGrass;
        }
    }
    Vector4 gPlateauTop = kPaletteGrass.plateauTop;
    Vector4 gCliff = kPaletteGrass.cliff;
    Vector4 gCliffHigh = kPaletteGrass.cliffHigh;
    Vector4 gRampTop = kPaletteGrass.rampTop;
    Vector4 gWallRock = kPaletteGrass.wallRock;
    Vector4 gGroundLight = kPaletteGrass.groundLight;   // 高台の坂の上面（草色）

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

    // 鉱洞の底：大きな湿りのむら + 細かい砂利
    Vector4 MineColor(float x, float z, uint32_t seed, const Palette& P)
    {
        const float big = ValueNoise(x / 14.0f, z / 14.0f, seed + 41u);
        const float fine = ValueNoise(x / 3.0f, z / 3.0f, seed + 43u);
        return LerpColor(P.mineDark, P.mineLight, std::clamp(big * 0.7f + fine * 0.45f - 0.15f, 0.0f, 1.0f));
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

    // 足跡の高さ場をそのまま h にする（鉱洞の底。低くもできる）
    void SetRectHeight(GridWorld& g, const Rect& r, float h)
    {
        const int sub = GridWorld::kHeightSub;
        for (int hz = r.z * sub; hz < (r.z + r.d) * sub; ++hz)
            for (int hx = r.x * sub; hx < (r.x + r.w) * sub; ++hx)
                g.SetHeightExact(hx, hz, h);
    }

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

    // 外へ 1 歩（Side の向き）
    void SideStep(Side s, int& dx, int& dz)
    {
        dx = (s == Side::PosX) ? 1 : (s == Side::NegX) ? -1 : 0;
        dz = (s == Side::PosZ) ? 1 : (s == Side::NegZ) ? -1 : 0;
    }
    Side Opposite(Side s)
    {
        switch (s)
        {
        case Side::PosX: return Side::NegX;
        case Side::NegX: return Side::PosX;
        case Side::PosZ: return Side::NegZ;
        default:         return Side::PosZ;
        }
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
        float base, float top, const Vector4& topColor = gRampTop)
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

        Entity e = SpawnHull(reg, batch, v, topColor, gCliff);
        WriteHullHeights(g, r, reg.Get<ColliderComponent>(e).hull, reg.Get<TransformComponent>(e).position);
        return e;
    }
}

namespace TerrainGenerator
{
    DirectX::SimpleMath::Vector4 GroundColor(float x, float z, uint32_t seed, Biome biome)
    {
        const Palette& P = PaletteFor(biome);
        switch (biome)
        {
        case Biome::Desert:
        {
            // 砂丘：大きなうねり + 風紋（x 方向の縞を雑音で曲げる）+ 所々の砂利
            const float big = ValueNoise(x / 30.0f, z / 30.0f, seed);
            const float bend = ValueNoise(x / 12.0f, z / 12.0f, seed + 7u) * 6.0f;
            const float ripple = 0.5f + 0.5f * std::sin((x + bend + z * 0.3f) * 0.9f);
            const Vector4 c = LerpColor(P.groundDark, P.groundLight,
                std::clamp(big * 0.7f + ripple * 0.35f + 0.05f, 0.0f, 1.0f));
            const float gravel = std::clamp((ValueNoise(x / 20.0f, z / 20.0f, seed + 13u) - 0.66f) * 4.0f, 0.0f, 0.5f);
            return LerpColor(c, P.groundDry, gravel);
        }
        case Biome::Dungeon:
        {
            // 石畳：2m のタイルごとに明るさを変え、所々に苔
            const int tx = (int)std::floor(x / 2.0f), tz = (int)std::floor(z / 2.0f);
            // 頂点色は 2m 毎の頂点で補間されるので、タイルの縁は出ない。目地の代わりに 4m 周期の縞で板を感じさせる
            const float tile = Hash01(tx, tz, seed + 3u);
            const float fine = ValueNoise(x / 4.0f, z / 4.0f, seed + 7u);
            const float seam = 0.5f + 0.5f * std::sin(x * 1.5708f) * std::sin(z * 1.5708f);
            const Vector4 c = LerpColor(P.groundDark, P.groundLight,
                std::clamp(tile * 0.45f + fine * 0.35f + seam * 0.3f, 0.0f, 1.0f));
            const float moss = std::clamp((ValueNoise(x / 18.0f, z / 18.0f, seed + 13u) - 0.6f) * 3.5f, 0.0f, 0.7f);
            return LerpColor(c, P.groundDry, moss);
        }
        default:
        {
            const float big = ValueNoise(x / 22.0f, z / 22.0f, seed);
            const float fine = ValueNoise(x / 6.0f, z / 6.0f, seed + 7u);
            const Vector4 c = LerpColor(P.groundDark, P.groundLight,
                std::clamp(big * 0.8f + fine * 0.4f - 0.1f, 0.0f, 1.0f));
            const float dry = std::clamp((ValueNoise(x / 35.0f, z / 35.0f, seed + 13u) - 0.62f) * 4.0f, 0.0f, 0.6f);
            return LerpColor(c, P.groundDry, dry);
        }
        }
    }

    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask,
        std::vector<Vector3>* outTorches, Layout* outLayout)
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

        // 面の色（helper が既定引数で参照する変数へ写す）
        {
            const Palette& P = PaletteFor(cfg.biome);
            gPlateauTop = P.plateauTop;
            gCliff = P.cliff;
            gCliffHigh = P.cliffHigh;
            gRampTop = P.rampTop;
            gWallRock = P.wallRock;
            gGroundLight = P.groundLight;
        }
        // 遺跡は外周を岩山でなく壁にする（衝突の箱は同じ）
        const bool ruinWalls = cfg.biome == Biome::Dungeon;

        // 静的な箱（衝突だけ。見た目は別）。lo / hi は世界座標の隅
        auto solidBox = [&](const Vector3& lo, const Vector3& hi)
            {
                outTerrain.push_back(TestSpawner::SpawnStaticBox(reg, (lo + hi) * 0.5f, (hi - lo) * 0.5f));
            };

        // ---------- 場地の三層：隅の山頂と鉱洞（2026-10-02）----------
        // zone: マス毎に 平原 / 山頂 / 鉱洞。山頂と鉱洞は対角の隅（どの組かは seed）。
        // 形 = 隅の正方形 → 内側の角の面取り → 内側の二辺に出っ張り 1〜2 個と凹み 1 個 → 1 マス幅の所を均す
        enum : uint8_t { kZonePlain = 0, kZoneSummit = 1, kZoneMine = 2 };
        std::vector<uint8_t> zone((size_t)gw * gd, kZonePlain);
        const bool layers = cfg.layers && gw >= 40 && gd >= 40;
        const float summitH = layers ? (std::max)(cfg.summitHeight, 1.0f) : 0.0f;
        const float mineD = layers ? (std::max)(cfg.mineDepth, 1.0f) : 0.0f;
        if (layers)
        {
            // 隅の局所座標 (u, v)：外周の崖の内側の隅のマス = (1, 1)、場地の内へ増える
            auto setLocal = [&](int corner, uint8_t id, int u0, int v0, int uw, int vw, bool on)
                {
                    for (int v = v0; v < v0 + vw; ++v)
                        for (int u = u0; u < u0 + uw; ++u)
                        {
                            const int gx = (corner & 1) ? (gw - 1 - u) : u;
                            const int gz = (corner & 2) ? (gd - 1 - v) : v;
                            if (gx < 1 || gz < 1 || gx >= gw - 1 || gz >= gd - 1) continue;
                            // 開局の場所（中央）には掛けない
                            if (std::abs(gx - gw / 2) <= cfg.spawnClearRadius + 3
                                && std::abs(gz - gd / 2) <= cfg.spawnClearRadius + 3) continue;
                            uint8_t& c = zone[(size_t)gz * gw + gx];
                            if (on) { if (c == kZonePlain) c = id; }
                            else if (c == id) c = kZonePlain;
                        }
                };
            auto makeZone = [&](int corner, int size, uint8_t id)
                {
                    size = std::clamp(size, 10, (std::min)(gw, gd) / 2 - 6);
                    setLocal(corner, id, 1, 1, size, size, true);
                    const int ch = randi(size / 8, size / 4);
                    setLocal(corner, id, 1 + size - ch, 1 + size - ch, ch, ch, false);
                    for (int edge = 0; edge < 2; ++edge)
                    {
                        // edge 0 = u が size の辺（v に沿う）、1 = v が size の辺（u に沿う）。
                        // along = 辺に沿った位置、depth = 辺から外（+）/ 内（-）
                        auto onEdge = [&](int along0, int alongLen, int depth0, int depthLen, bool on)
                            {
                                if (edge == 0) setLocal(corner, id, 1 + size + depth0, along0, depthLen, alongLen, on);
                                else           setLocal(corner, id, along0, 1 + size + depth0, alongLen, depthLen, on);
                            };
                        const int lobes = randi(1, 2);
                        for (int i = 0; i < lobes; ++i)
                        {
                            const int len = randi(size / 6, size / 3);
                            onEdge(randi(3, size - ch - len - 2), len, 0, randi(3, (std::max)(3, size / 7)), true);
                        }
                        const int len = randi(size / 8, size / 5);
                        const int dep = randi(3, (std::max)(3, size / 8));
                        onEdge(randi(3, size - ch - len - 2), len, -dep, dep, false);
                    }
                };
            const int summitCorner = randi(0, 3);
            makeZone(summitCorner, cfg.summitSize, kZoneSummit);
            makeZone(summitCorner ^ 3, cfg.mineSize, kZoneMine);

            // 1 マス幅の飛び出し・切れ込みを均す（細い崖の歯は雑魚が詰まり、見た目も汚い）
            for (int pass = 0; pass < 2; ++pass)
                for (uint8_t id : { kZoneSummit, kZoneMine })
                {
                    std::vector<uint8_t> next = zone;
                    auto is = [&](int x, int z) { return zone[(size_t)z * gw + x] == id; };
                    for (int z = 2; z < gd - 2; ++z)
                        for (int x = 2; x < gw - 2; ++x)
                        {
                            const bool thinX = !is(x - 1, z) && !is(x + 1, z);
                            const bool thinZ = !is(x, z - 1) && !is(x, z + 1);
                            const bool gapX = is(x - 1, z) && is(x + 1, z);
                            const bool gapZ = is(x, z - 1) && is(x, z + 1);
                            uint8_t& c = next[(size_t)z * gw + x];
                            if (is(x, z) && (thinX || thinZ)) c = kZonePlain;
                            else if (c == kZonePlain && (gapX || gapZ)) c = id;
                        }
                    zone.swap(next);
                }
        }
        // 鉱洞の屋根（2026-10-03）：坑の周り 1 マスの岩の壁（caveRing、下り坂の口は除く）。見た目の高さは roofTopY。
        // 中身は下り坂を置いた後で決まる
        const bool cave = layers && cfg.mineRoof;
        const float roofBottomY = cave ? (std::max)(cfg.roofBottom, 2.5f) : 0.0f;
        const float roofTopY = cave ? (std::max)(cfg.roofTop, roofBottomY + 1.0f) : 0.0f;
        std::vector<uint8_t> caveRing((size_t)gw * gd, 0);

        // マス毎の地面の高さ（見た目）。外周の崖のマスは内側の隣と同じ（場地の縁に段の壁を作らない）
        auto zoneAt = [&](int x, int z)
            {
                x = std::clamp(x, 1, gw - 2);
                z = std::clamp(z, 1, gd - 2);
                return zone[(size_t)z * gw + x];
            };
        auto levelAt = [&](int x, int z)
            {
                const int cx = std::clamp(x, 1, gw - 2), cz = std::clamp(z, 1, gd - 2);
                if (caveRing[(size_t)cz * gw + cx]) return roofTopY;   // 洞の岩の壁（高さ場は 0 のまま、塞いだマス）
                const uint8_t id = zoneAt(x, z);
                return (id == kZoneSummit) ? summitH : (id == kZoneMine) ? -mineD : 0.0f;
            };
        auto levelAtWorld = [&](float x, float z)
            {
                int gx = 0, gz = 0;
                grid.WorldToCell({ x, 0.0f, z }, gx, gz);
                return levelAt(gx, gz);
            };

        // ---------- 床（草地）----------
        // 見た目: マス毎の高さの段々のメッシュ 1 つ（平原・山頂の上面は値ノイズの色むら、鉱洞の底は土と砂利、
        //   段の境は 2m 毎の縞の岩の壁）。
        // 衝突: 鉱洞以外は上面 y = 0 の箱（底は鉱洞の底より下 = 鉱洞の壁にもなる）、鉱洞の底は上面 -mineDepth、
        //   山頂は 0〜summitHeight。どれもマスの印を矩形に分けた箱の組。
        // 高さ場: 山頂は上げる、鉱洞はそのまま -mineDepth を書く（坂は後で高い方を書く）。
        // 床は格子に登記しない（上を歩くものなので通行を塞がない）
        const float floorBottom = -mineD - 1.0f;
        {
            std::vector<uint8_t> solid((size_t)gw * gd);
            for (size_t i = 0; i < solid.size(); ++i) solid[i] = (zone[i] == kZoneMine) ? 0 : 1;
            for (const Rect& r : MaskRects(solid, gw, gd, 1))
            {
                const Vector3 lo = RectMin(grid, r);
                solidBox({ lo.x, floorBottom, lo.z }, lo + Vector3(r.w * kCs, 0.0f, r.d * kCs));
            }
            for (const Rect& r : MaskRects(zone, gw, gd, kZoneMine))
            {
                const Vector3 lo = RectMin(grid, r);
                solidBox({ lo.x, floorBottom, lo.z }, lo + Vector3(r.w * kCs, -mineD, r.d * kCs));
                SetRectHeight(grid, r, -mineD);
            }
            for (const Rect& r : MaskRects(zone, gw, gd, kZoneSummit))
            {
                const Vector3 lo = RectMin(grid, r);
                solidBox(lo, lo + Vector3(r.w * kCs, summitH, r.d * kCs));
                RaiseRect(grid, r, summitH);
            }
            // 見た目のメッシュは洞の岩の壁が決まってから（下り坂の後）
        }

        // ---------- 外周の崖 ----------
        // 1 マス幅で四辺を囲む。場外へ出る・落ちるをここで殺す（格子も塞ぐ）。
        // 高さは鉱洞の底の下から山頂 + wallHeight まで（山頂から外へ飛び出せない）。
        // 岩山にする時は衝突の箱だけ（見た目は後で岩を積む）
        auto wall = [&](int x, int z, int w, int d)
            {
                const Vector3 lo = RectMin(grid, { x, z, w, d }) + Vector3(0.0f, floorBottom - 1.0f, 0.0f);
                const Vector3 hi = RectMin(grid, { x, z, w, d }) + Vector3(w * kCs, summitH + cfg.wallHeight, d * kCs);
                if (cfg.rockMountains || ruinWalls)
                    outTerrain.push_back(TestSpawner::SpawnStaticBox(reg, (lo + hi) * 0.5f, (hi - lo) * 0.5f));
                else
                    outTerrain.push_back(SpawnBlock(reg, batch, lo, hi, gPlateauTop, gWallRock));
                grid.BlockArea(x, z, w, d);
            };
        wall(0, 0, gw, 1);
        wall(0, gd - 1, gw, 1);
        wall(0, 1, 1, gd - 2);
        wall(gw - 1, 1, 1, gd - 2);

        // ---------- 占有図 ----------
        // 空き地 / 台地・高台の上面 / 坂道 / 初期地点 / 坂道の降り口（空き地のまま残す）/ 高台の長い坂 /
        // 高台の坂の麓（地面のまま。木・岩・台地は置かない。茂み・草は置く）/ 高台の上の通り道（木・岩を置かない）/
        // 山頂の上面（木・岩を置く）/ 鉱洞の底（岩だけ置く）
        enum : uint8_t { kFree, kPlateau, kRamp, kSpawn, kLanding, kSlope, kApron, kWay, kHigh, kPit, kMass };   // kMass = 洞の岩の壁
        std::vector<uint8_t> occ((size_t)gw * gd, kFree);
        for (size_t i = 0; i < occ.size(); ++i)
            occ[i] = (zone[i] == kZoneSummit) ? kHigh : (zone[i] == kZoneMine) ? kPit : kFree;
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
        const int clear = (std::max)(cfg.terraceClear, 1);
        // 坂は場外の壁に向けない：麓から下る向きに壁まで 12 マス（24m。20 m/s で滑り出しても曲がる余裕）
        constexpr int kWallRunout = 12;
        auto toWallFrom = [&](const Rect& r, Side s)
            {
                switch (s)
                {
                case Side::PosX: return (gw - 1) - (r.x + r.w);
                case Side::NegX: return r.x - 1;
                case Side::PosZ: return (gd - 1) - (r.z + r.d);
                default:         return r.z - 1;
                }
            };
        // 矩形のマスが全部 ok のどれか（場外は不可）
        auto allOcc = [&](const Rect& r, std::initializer_list<uint8_t> ok)
            {
                for (int z = r.z; z < r.z + r.d; ++z)
                    for (int x = r.x; x < r.x + r.w; ++x)
                    {
                        if (x < 0 || z < 0 || x >= gw || z >= gd) return false;
                        if (std::find(ok.begin(), ok.end(), at(x, z)) == ok.end()) return false;
                    }
                return true;
            };
        // 既に置いた坂（の中心）から gap マス以上離れているか
        auto farFrom = [](const std::vector<Rect>& made, const Rect& r, int gap)
            {
                for (const Rect& m : made)
                {
                    const float dx = (m.x + m.w * 0.5f) - (r.x + r.w * 0.5f);
                    const float dz = (m.z + m.d * 0.5f) - (r.z + r.d * 0.5f);
                    if (dx * dx + dz * dz < (float)(gap * gap)) return false;
                }
                return true;
            };

        // ---------- 山頂の長い坂・鉱洞の下り坂 ----------
        // 区域の縁のうち、幅 w マスが真っ直ぐ同じ向きに平原へ面している所に、縁と直角の坂を置く。
        //   山頂: 坂は縁の外（平原）へ下る。麓の先 terraceClear マスは空き地（kApron）、坂の上端の山頂 2 マスは通り道（kWay）。
        //   鉱洞: 坂は縁の内（底）へ下る（坂の両脇は底へ飛び降りられる）。入口の平原側 3 マスと底の降り口 1 マスは空ける（kLanding）。
        // 候補を全部集めて混ぜ、互いに離れた物から本数分取る
        int summitRampsMade = 0, mineRampsMade = 0;
        std::vector<Rect> mineLandings;   // 鉱洞の坂の降り口（底側）。一番奥を探す起点
        std::vector<Rect> mineMouths;     // 鉱洞の坂の上端の外 1 列（平原側）= 洞の口
        std::vector<Layout::Ramp> summitRampInfo, mineRampInfo;
        // 坂の上端の辺の中央と下る向き（Layout へ渡す）
        auto rampInfo = [&](const Rect& r, Side down, float topY)
            {
                int dx, dz;
                SideStep(down, dx, dz);
                const Vector3 c = RectMin(grid, r) + Vector3(r.w * kCs * 0.5f, 0.0f, r.d * kCs * 0.5f);
                const float half = ((dx != 0) ? r.w : r.d) * kCs * 0.5f;
                Layout::Ramp info;
                info.down = Vector3((float)dx, 0.0f, (float)dz);
                info.top = c - info.down * half;
                info.top.y = topY;
                return info;
            };
        if (layers)
        {
            const Side sides[4] = { Side::PosX, Side::NegX, Side::PosZ, Side::NegZ };
            auto zoneId = [&](int x, int z) -> int
                { return (x < 0 || z < 0 || x >= gw || z >= gd) ? -1 : (int)zone[(size_t)z * gw + x]; };
            // (x, z) から幅 w の縁（s の向きの隣が平原、自分は id）か
            auto edgeRun = [&](int x, int z, Side s, int w, int id)
                {
                    int dx, dz;
                    SideStep(s, dx, dz);
                    for (int k = 0; k < w; ++k)
                    {
                        const int cx = (dx != 0) ? x : x + k, cz = (dx != 0) ? z + k : z;
                        if (zoneId(cx, cz) != id || zoneId(cx + dx, cz + dz) != kZonePlain) return false;
                    }
                    return true;
                };
            struct Cand { Rect ramp; Side s; Rect extra; Rect apron; };

            // ---- 山頂 ----
            // 麓の空き地は高台より長く取る（16m を滑り降りると 20 m/s 近く出る）。
            // 300m の頃は 24m（12 マス）、200m に戻して 2/3 の 16m。壁までは kWallRunout
            {
                constexpr int kSummitRunout = 8;
                const int w = (std::max)(1, cfg.summitRampWidth);
                const int runout = (std::max)(clear, kSummitRunout);
                const float tanS = std::tan(DirectX::XMConvertToRadians(
                    std::clamp(randf(cfg.summitRampSlopeMin, cfg.summitRampSlopeMax), 5.0f, 35.0f)));
                const int len = (std::max)(1, (int)std::ceil(summitH / tanS / kCs));
                std::vector<Cand> cands;
                for (Side s : sides)
                    for (int z = 1; z < gd - 1; ++z)
                        for (int x = 1; x < gw - 1; ++x)
                        {
                            if (!edgeRun(x, z, s, w, kZoneSummit)) continue;
                            Rect r, way;
                            switch (s)
                            {
                            case Side::PosX: r = { x + 1, z, len, w };   way = { x - 1, z, 2, w }; break;
                            case Side::NegX: r = { x - len, z, len, w }; way = { x, z, 2, w };     break;
                            case Side::PosZ: r = { x, z + 1, w, len };   way = { x, z - 1, w, 2 }; break;
                            default:         r = { x, z - len, w, len }; way = { x, z, w, 2 };     break;
                            }
                            const Rect apron = RampRect(r, s, -1, w + 2, runout);   // 坂の幅 + 左右 1 マス
                            if (!inside(r) || !isFree(r, 0)) continue;
                            if (apron.x < 1 || apron.z < 1 || apron.x + apron.w > gw - 1 || apron.z + apron.d > gd - 1) continue;
                            if (toWallFrom(r, s) < (std::max)(runout, kWallRunout)) continue;
                            if (!allOcc(apron, { kFree, kApron, kSpawn })) continue;
                            cands.push_back({ r, s, way, apron });
                        }
                std::shuffle(cands.begin(), cands.end(), rng);
                std::vector<Rect> made;
                for (const Cand& c : cands)
                {
                    if ((int)made.size() >= cfg.summitRamps) break;
                    if (!farFrom(made, c.ramp, w + 8)) continue;
                    if (!isFree(c.ramp, 0) || !allOcc(c.apron, { kFree, kApron, kSpawn })) continue;   // 先に置いた坂の麓
                    for (int z = c.apron.z; z < c.apron.z + c.apron.d; ++z)
                        for (int x = c.apron.x; x < c.apron.x + c.apron.w; ++x)
                            if (at(x, z) == kFree) at(x, z) = kApron;
                    mark(c.ramp, kSlope);
                    mark(c.extra, kWay);
                    outTerrain.push_back(SpawnRamp(reg, batch, grid, c.ramp, c.s, 0.0f, summitH, Jitter(gGroundLight, rng, 0.02f)));
                    summitRampInfo.push_back(rampInfo(c.ramp, c.s, summitH));
                    made.push_back(c.ramp);
                }
                summitRampsMade = (int)made.size();
            }

            // ---- 鉱洞 ----
            {
                const int w = (std::max)(1, cfg.mineRampWidth);
                const float tanM = std::tan(DirectX::XMConvertToRadians(std::clamp(cfg.mineRampSlope, 5.0f, 35.0f)));
                const int len = (std::max)(1, (int)std::ceil(mineD / tanM / kCs));
                std::vector<Cand> cands;
                for (Side s : sides)   // s = 平原のある向き。坂は逆向き（底）へ下る
                    for (int z = 1; z < gd - 1; ++z)
                        for (int x = 1; x < gw - 1; ++x)
                        {
                            if (!edgeRun(x, z, s, w, kZoneMine)) continue;
                            Rect r, entry;
                            switch (s)
                            {
                            case Side::PosX: r = { x - len + 1, z, len, w }; entry = { x + 1, z, 3, w }; break;
                            case Side::NegX: r = { x, z, len, w };           entry = { x - 3, z, 3, w }; break;
                            case Side::PosZ: r = { x, z - len + 1, w, len }; entry = { x, z + 1, w, 3 }; break;
                            default:         r = { x, z, w, len };           entry = { x, z - 3, w, 3 }; break;
                            }
                            const Rect land = LandingRect(r, Opposite(s));
                            if (!inside(entry)) continue;
                            // 坂・降り口は鉱洞の底（kPit = 鉱洞の中だけ）、入口は平原の空き地
                            if (!allOcc(r, { kPit }) || !allOcc(land, { kPit }) || !allOcc(entry, { kFree })) continue;
                            cands.push_back({ r, s, land, entry });
                        }
                std::shuffle(cands.begin(), cands.end(), rng);
                std::vector<Rect> made;
                for (const Cand& c : cands)
                {
                    if ((int)made.size() >= cfg.mineRamps) break;
                    if (!farFrom(made, c.ramp, w + 10)) continue;
                    if (!allOcc(c.ramp, { kPit }) || !allOcc(c.extra, { kPit }) || !allOcc(c.apron, { kFree })) continue;
                    mark(c.ramp, kRamp);
                    mark(c.extra, kLanding);
                    mark(c.apron, kLanding);
                    outTerrain.push_back(SpawnRamp(reg, batch, grid, c.ramp, Opposite(c.s), -mineD, 0.0f));
                    mineRampInfo.push_back(rampInfo(c.ramp, Opposite(c.s), 0.0f));
                    mineLandings.push_back(c.extra);
                    // 入口の帯（3 列）のうち坑に接する 1 列
                    const Rect& e = c.apron;
                    switch (c.s)
                    {
                    case Side::PosX: mineMouths.push_back({ e.x, e.z, 1, e.d }); break;
                    case Side::NegX: mineMouths.push_back({ e.x + e.w - 1, e.z, 1, e.d }); break;
                    case Side::PosZ: mineMouths.push_back({ e.x, e.z, e.w, 1 }); break;
                    default:         mineMouths.push_back({ e.x, e.z + e.d - 1, e.w, 1 }); break;
                    }
                    made.push_back(c.ramp);
                }
                mineRampsMade = (int)made.size();
            }
        }

        // ---------- 鉱洞の屋根（2026-10-03、用户：推奨どおり）----------
        // 坑の上を岩の塊で覆い、外から見ると山の麓の洞穴にする。
        //   岩の壁 = 坑の 8 近傍の平原のマス（下り坂の口を除く）。格子を塞ぎ（雑魚は縁から飛び降りられない。
        //     出入りは下り坂だけ）、見た目は roofTop の高さの段（段々のメッシュが外の壁・中の壁を描く）。
        //   屋根 = 坑の上の roofBottom〜roofTop の板、口の上 = 同じ高さの梁（口の高さは roofBottom）。
        //   衝突は壁・屋根・梁とも roofCollisionTop まで（見えない。上に乗れない）
        int caveRingCells = 0;
        if (cave)
        {
            std::vector<uint8_t> mouth((size_t)gw * gd, 0);
            for (const Rect& m : mineMouths)
                for (int z = m.z; z < m.z + m.d; ++z)
                    for (int x = m.x; x < m.x + m.w; ++x)
                        if (x >= 0 && z >= 0 && x < gw && z < gd) mouth[(size_t)z * gw + x] = 1;
            for (int z = 1; z < gd - 1; ++z)
                for (int x = 1; x < gw - 1; ++x)
                {
                    const size_t i = (size_t)z * gw + x;
                    if (zone[i] == kZoneMine || mouth[i]) continue;
                    bool nextToPit = false;
                    for (int dz = -1; dz <= 1 && !nextToPit; ++dz)
                        for (int dx = -1; dx <= 1 && !nextToPit; ++dx)
                            nextToPit = zone[(size_t)(z + dz) * gw + (x + dx)] == kZoneMine;
                    if (!nextToPit) continue;
                    caveRing[i] = 1;
                    at(x, z) = kMass;
                    grid.BlockArea(x, z, 1, 1);
                    ++caveRingCells;
                }

            const float collTop = (std::max)(cfg.roofCollisionTop, roofTopY);
            const Vector4 rockTop(gCliffHigh.x * 1.1f, gCliffHigh.y * 1.1f, gCliffHigh.z * 1.1f, 1.0f);
            for (const Rect& r : MaskRects(caveRing, gw, gd, 1))
            {
                const Vector3 lo = RectMin(grid, r);
                solidBox(lo, lo + Vector3(r.w * kCs, collTop, r.d * kCs));
            }
            auto roofOver = [&](const std::vector<uint8_t>& mask, uint8_t value)
                {
                    for (const Rect& r : MaskRects(mask, gw, gd, value))
                    {
                        const Vector3 lo = RectMin(grid, r);
                        solidBox(lo + Vector3(0.0f, roofBottomY, 0.0f), lo + Vector3(r.w * kCs, collTop, r.d * kCs));
                        Vector3 v[8];
                        BoxVerts(v, lo + Vector3(0.0f, roofBottomY, 0.0f), lo + Vector3(r.w * kCs, roofTopY, r.d * kCs));
                        batch.Append(v, rockTop, gCliffHigh);
                    }
                };
            roofOver(zone, kZoneMine);   // 屋根
            roofOver(mouth, 1);          // 口の上の梁
        }

        // ---------- 床の見た目（段々のメッシュ）----------
        {
            const Palette& P = PaletteFor(cfg.biome);
            const uint32_t s = cfg.seed;
            const Biome bm = cfg.biome;
            const float rockLevel = roofTopY;
            auto topColor = [&P, s, bm, cave, rockLevel](float x, float z, float level)
                {
                    if (cave && std::fabs(level - rockLevel) < 0.01f)   // 洞の岩の壁の上
                    {
                        const float k = 0.95f + 0.25f * ValueNoise(x / 5.0f, z / 5.0f, s + 51u);
                        return Vector4(gCliffHigh.x * k, gCliffHigh.y * k, gCliffHigh.z * k, 1.0f);
                    }
                    return (level < -0.01f) ? MineColor(x, z, s, P) : GroundColor(x, z, s, bm);
                };
            // 壁：山頂の崖・洞の岩は灰色がかった岩（2 段目の台地と同じ色）、鉱洞の壁は土と岩。2m の帯ごとに明るさを変える（地層）
            auto wallColor = [s](float x, float y, float z)
                {
                    const Vector4& base = (y > 0.0f) ? gCliffHigh : gCliff;
                    const float band = Hash01((int)std::floor(y / 2.0f), 17, s);
                    const float drift = ValueNoise((x + z) / 9.0f, y / 4.0f, s + 31u);
                    const float k = (0.82f + 0.3f * band) * (0.9f + 0.2f * drift);
                    return Vector4(base.x * k, base.y * k, base.z * k, 1.0f);
                };
            Entity e = reg.Create();
            reg.Add<TransformComponent>(e, TransformComponent{});
            ModelComponent mc;
            mc.model = PrimitiveBuilder::CreateSteppedGrid(device, gw, gd, kCs, levelAt, topColor, wallColor, 2.0f);
            reg.Add<ModelComponent>(e, mc);
            outTerrain.push_back(e);
        }

        // ---------- 高台（一面だけが長い坂、残り三面は崖。一部は上に 2 段目）----------
        // 場所を大きく取るので台地より先に置く。坂の向きは高台ごとにランダム。
        //   1 段目: 上面 p（高さ h1）+ その辺いっぱいの幅の坂（長さ = h1 / tan 角度）
        //   2 段目: p の奥に上面（+h2）と、同じ向きの坂。p の坂側 2 マスは 1 段目のまま残す通り道、
        //           2 段目の坂以外の周りは 1 マスの縁を残す → 地面・1 段目・2 段目の 3 段が見える
        //   通り道（kWay）には木・岩を置かない（2 段目 → 1 段目 → 地面と滑り継げる）。
        //   坂の麓の先 terraceClear マスは滑り出す空き地（kApron）。場外の壁ともこれだけ空ける
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
            // 坂は場外の壁に向けない（kWallRunout）
            if (toWallFrom(sr, s) < (std::max)(kWallRunout, clear)) continue;
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
                Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliff, rng, 0.015f)));
            RaiseRect(grid, p, h1);
            outTerrain.push_back(SpawnRamp(reg, batch, grid, sr, s, 0.0f, h1, Jitter(gGroundLight, rng, 0.02f)));

            if (tier2)
            {
                const int w2 = randi(3, across - 2);
                const int v0 = randi(1, across - 1 - w2);
                const Rect t = LocalRect(p, s, 2 + run2, top2, v0, w2);
                const Rect s2 = LocalRect(p, s, 2, run2, v0, w2);
                const Vector3 tlo = RectMin(grid, t);
                outTerrain.push_back(SpawnBlock(reg, batch, tlo + Vector3(0.0f, h1, 0.0f),
                    tlo + Vector3(t.w * kCs, h1 + h2, t.d * kCs),
                    Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliffHigh, rng, 0.015f)));
                RaiseRect(grid, t, h1 + h2);
                outTerrain.push_back(SpawnRamp(reg, batch, grid, s2, s, h1, h1 + h2, Jitter(gGroundLight, rng, 0.02f)));
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
                Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliff, rng, 0.015f)));
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
                Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliffHigh, rng, 0.015f)));
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
        // 面ごとの自然物の表（木 = 塞ぐ縦長の物、岩 = 塞ぐ物、茂み = 見た目だけ、崖 = 外周の岩山）。
        // 砂漠 / 遺跡は数を減らし、枯れ木の割合を上げる
        struct PropSet
        {
            const char* const* trees; size_t nTrees;
            const char* const* bare;  size_t nBare;
            const char* const* rocks; size_t nRocks;
            const char* const* bushes; size_t nBushes;
            const char* const* cliff; size_t nCliff;
            float treeMul, rockMul, bushMul, bareRatio;
        };
        namespace F = Res::Mdl::Forest;
        namespace Ds = Res::Mdl::Desert;
        namespace Ru = Res::Mdl::Ruins;
        PropSet set = { F::kTrees, std::size(F::kTrees), F::kBareTrees, std::size(F::kBareTrees),
            F::kRocks, std::size(F::kRocks), F::kBushes, std::size(F::kBushes),
            F::kCliffRocks, std::size(F::kCliffRocks), 1.0f, 1.0f, 1.0f, 0.1f };
        if (cfg.biome == Biome::Desert)
            set = { Ds::kTrees, std::size(Ds::kTrees), Ds::kBareTrees, std::size(Ds::kBareTrees),
                Ds::kRocks, std::size(Ds::kRocks), Ds::kBushes, std::size(Ds::kBushes),
                Ds::kCliffRocks, std::size(Ds::kCliffRocks), 0.6f, 1.4f, 1.1f, 0.55f };
        else if (cfg.biome == Biome::Dungeon)
            set = { Ru::kTrees, std::size(Ru::kTrees), Ru::kBareTrees, std::size(Ru::kBareTrees),
                Ru::kRocks, std::size(Ru::kRocks), Ru::kBushes, std::size(Ru::kBushes),
                nullptr, 0, 0.6f, 1.0f, 0.7f, 0.15f };
        const auto trees = loadModels(set.trees, set.nTrees);
        const auto bareTrees = loadModels(set.bare, set.nBare);
        const auto rocks = loadModels(set.rocks, set.nRocks);
        const auto bushes = loadModels(set.bushes, set.nBushes);
        const int treeTarget = (int)(cfg.treeCount * set.treeMul);
        const int rockTarget = (int)(cfg.rockCount * set.rockMul);
        const int bushTarget = (int)(cfg.bushCount * set.bushMul);

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

        // 置物 1 個。回した包囲箱の足跡（木は幹のある 1 マスだけ）を調べて置く。
        // 小さい物（木は高さ < treeBlockMinHeight、岩は足跡の長辺 < rockBlockMinSize）は見た目だけ:
        // 置き場所の条件と「周りに他の置物無し」は同じだが、格子を塞がず衝突も付けない
        int decorTrees = 0, decorRocks = 0;
        auto placeBlocker = [&](const PropModel& pm, float scale, float yawDeg, float x, float z, bool isTree) -> bool
            {
                const float u = pm.unit * scale;
                const float yaw = DirectX::XMConvertToRadians(yawDeg);
                const float cs = std::fabs(std::cos(yaw)), sn = std::fabs(std::sin(yaw));
                const float ex = (pm.hi.x - pm.lo.x) * 0.5f * u, ez = (pm.hi.z - pm.lo.z) * 0.5f * u;
                const float hx = cs * ex + sn * ez, hz = sn * ex + cs * ez;   // 回した後の半幅
                const float height = (pm.hi.y - pm.lo.y) * u;
                const bool blocks = isTree ? (height >= cfg.treeBlockMinHeight)
                    : ((std::max)(ex, ez) * 2.0f >= cfg.rockBlockMinSize);

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
                        // 空き地・台地・山頂の上面。鉱洞の底は岩だけ
                        const uint8_t o = at(xx, zz);
                        const bool okHere = o == kFree || o == kPlateau || o == kHigh || (!isTree && o == kPit);
                        if (!okHere || !grid.IsWalkable(xx, zz) || propAt[(size_t)zz * gw + xx]) return false;
                        if (std::fabs(cellHeight(xx, zz) - h) > 0.2f) return false;
                    }

                for (int zz = gz0; zz <= gz1; ++zz)
                    for (int xx = gx0; xx <= gx1; ++xx)
                        propAt[(size_t)zz * gw + xx] = 1;

                spawnVisual(pm, scale, yawDeg, x, z, h);
                if (!blocks)
                {
                    ++(isTree ? decorTrees : decorRocks);
                    return true;
                }
                grid.BlockArea(gx0, gz0, gx1 - gx0 + 1, gz1 - gz0 + 1);
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
        for (int attempt = 0; attempt < treeTarget * 40 && treesPlaced < treeTarget && !trees.empty(); ++attempt)
        {
            float x, z;
            randomPoint(x, z);
            const bool grove = ValueNoise(x / 26.0f, z / 26.0f, cfg.seed + 21u) > 0.58f;
            if (!grove && randf(0.0f, 1.0f) > 0.08f) continue;
            const bool bare = !bareTrees.empty() && randf(0.0f, 1.0f) < set.bareRatio;
            const auto& list = bare ? bareTrees : trees;
            const PropModel& pm = list[randi(0, (int)list.size() - 1)];
            if (placeBlocker(pm, randf(0.9f, 1.3f), randf(0.0f, 360.0f), x, z, true)) ++treesPlaced;
        }

        int rocksPlaced = 0;
        for (int attempt = 0; attempt < rockTarget * 40 && rocksPlaced < rockTarget && !rocks.empty(); ++attempt)
        {
            float x, z;
            randomPoint(x, z);
            const PropModel& pm = rocks[randi(0, (int)rocks.size() - 1)];
            if (placeBlocker(pm, randf(0.8f, 1.5f), randf(0.0f, 360.0f), x, z, false)) ++rocksPlaced;
        }

        // 鉱洞の底の岩（崩れた岩屑。木は生やさない）。底のマスから選ぶ
        int mineRocksPlaced = 0;
        if (layers && !rocks.empty())
        {
            std::vector<int> pit;
            for (int i = 0; i < gw * gd; ++i)
                if (occ[(size_t)i] == kPit) pit.push_back(i);
            const int target = (int)(cfg.mineRockCount * set.rockMul);
            for (int attempt = 0; attempt < target * 30 && mineRocksPlaced < target && !pit.empty(); ++attempt)
            {
                const int c = pit[randi(0, (int)pit.size() - 1)];
                const Vector3 p = grid.CellToWorld(c % gw, c / gw);
                const PropModel& pm = rocks[randi(0, (int)rocks.size() - 1)];
                if (placeBlocker(pm, randf(0.9f, 1.8f), randf(0.0f, 360.0f),
                        p.x + randf(-0.6f, 0.6f), p.z + randf(-0.6f, 0.6f), false))
                    ++mineRocksPlaced;
            }
        }

        // 茂み：平らな所（坂道・坂の上以外）。半分を林の中へ。
        // 草は模型ではなく GrassRenderer が GPU で生やす（下の outGrassMask）
        int bushesPlaced = 0;
        for (int attempt = 0; attempt < bushTarget * 10 && bushesPlaced < bushTarget && !bushes.empty(); ++attempt)
        {
            float x, z;
            randomPoint(x, z);
            if (randf(0.0f, 1.0f) < 0.5f && ValueNoise(x / 26.0f, z / 26.0f, cfg.seed + 21u) <= 0.58f) continue;
            int gx, gz;
            grid.WorldToCell({ x, 0.0f, z }, gx, gz);
            if (!grid.IsWalkable(gx, gz) || at(gx, gz) == kRamp || zoneAt(gx, gz) == kZoneMine) continue;
            const float h = grid.SampleHeight(x, z);
            if (std::fabs(grid.SampleHeight(x + 0.6f, z) - h) > 0.1f
                || std::fabs(grid.SampleHeight(x, z + 0.6f) - h) > 0.1f
                || std::fabs(grid.SampleHeight(x - 0.6f, z) - h) > 0.1f
                || std::fabs(grid.SampleHeight(x, z - 0.6f) - h) > 0.1f) continue;
            spawnVisual(bushes[randi(0, (int)bushes.size() - 1)], randf(0.8f, 1.3f), randf(0.0f, 360.0f), x, z, h);
            ++bushesPlaced;
        }

        // 場地の縁の地面の高さ（外周の岩山・遺跡の壁の底と頂を決める）。
        // n = 外向きの法線、t = 辺に沿う向き、edge = 縁までの距離。辺に沿った s ± halfLen の 3 点の最低 lo / 最高 hi
        // （山頂の脇は高く、鉱洞の脇は低い。段の境に掛かる物は低い方から高い方まで届かせる）
        auto edgeSpan = [&](const Vector3& n, const Vector3& t, float edge, float s, float halfLen, float& lo, float& hi)
            {
                lo = 1e9f; hi = -1e9f;
                const float limX = W * 0.5f - kCs * 1.5f, limZ = D * 0.5f - kCs * 1.5f;   // 縁の内側のマスの中
                for (int k = -1; k <= 1; ++k)
                {
                    const Vector3 p = n * (edge - kCs * 0.5f) + t * (s + k * halfLen);
                    const float h = levelAtWorld(std::clamp(p.x, -limX, limX), std::clamp(p.z, -limZ, limZ));
                    lo = (std::min)(lo, h);
                    hi = (std::max)(hi, h);
                }
            };

        // ---------- 外周の岩山（rockMountains）----------
        // 大きい岩を拡大して、崖の箱の外側へ 3 列に隙間なく積む（見た目だけ。衝突・格子は崖の箱）。
        // 手前の列は内側の面が場地の縁（崖のマスの内側）に来るように置き、奥ほど高く。
        // 四辺とも角の先まで伸ばして、角に穴が開かないようにする。
        // 底は縁の地面の高さ（鉱洞の脇は鉱洞の底から）、高さは平原（山頂の脇は山頂）から測る
        int mountainRocks = 0;
        if (cfg.rockMountains && !ruinWalls && set.nCliff > 0)
        {
            const auto cliffRocks = loadModels(set.cliff, set.nCliff);
            struct Row { float offset, step, hMin, hMax; };
            const Row rows[] = {
                { 0.0f, 4.5f, 8.0f, 13.0f },
                { 7.0f, 6.0f, 15.0f, 22.0f },
                { 16.0f, 8.0f, 24.0f, 34.0f },
            };
            const float halfW = W * 0.5f - kCs, halfD = D * 0.5f - kCs;   // 場地の縁（崖のマスの内側）
            // 四辺: 外向きの法線 n と、辺に沿う向き t、辺の半分の長さ
            struct Side { Vector3 n, t; float edge, half; };
            const Side sides[] = {
                { { 0, 0, 1 }, { 1, 0, 0 }, halfD, halfW },
                { { 0, 0, -1 }, { 1, 0, 0 }, halfD, halfW },
                { { 1, 0, 0 }, { 0, 0, 1 }, halfW, halfD },
                { { -1, 0, 0 }, { 0, 0, 1 }, halfW, halfD },
            };
            for (const Side& sd : sides)
                for (const Row& row : rows)
                {
                    const float extra = row.offset + 20.0f;   // 角の先まで
                    for (float s = -sd.half - extra; s <= sd.half + extra; s += row.step)
                    {
                        const PropModel& pm = cliffRocks[randi(0, (int)cliffRocks.size() - 1)];
                        const float h = (pm.hi.y - pm.lo.y) * pm.unit;
                        if (h < 0.1f) continue;
                        float lo, hi;
                        edgeSpan(sd.n, sd.t, sd.edge, s, row.step * 0.75f, lo, hi);
                        const float targetH = randf(row.hMin, row.hMax) * cfg.mountainScale + ((std::max)(hi, 0.0f) - lo);
                        const float scale = targetH / h;   // unit を含めた倍率は spawnVisual が掛ける
                        const float radius = 0.5f * ((pm.hi.x - pm.lo.x) + (pm.hi.z - pm.lo.z)) * 0.5f * pm.unit * scale;
                        const float out = sd.edge + row.offset + radius * 0.8f + randf(-1.0f, 1.0f);
                        const Vector3 p = sd.n * out + sd.t * (s + randf(-1.0f, 1.0f));
                        // 少し埋める（底の縁が浮いて見えないように）
                        spawnVisual(pm, scale, randf(0.0f, 360.0f), p.x, p.z, lo - targetH * 0.08f);
                        ++mountainRocks;
                    }
                }
        }

        // ---------- 遺跡の壁（Biome::Dungeon の外周）----------
        // Modular Ruins の 2m 幅の壁を辺に沿って並べ、壁の高さ（cfg.wallHeight）まで積む。一番上の段は崩れた縁。
        // 内側の面が場地の縁（崖のマスの内側）に来る。柱を 8m 毎、松明を 12m 毎に内側の面へ
        // （松明の位置は outTorches へ。点光源は場面が近い物にだけ付ける）。
        // 衝突・格子は崖の箱のまま（壁は見た目だけ）
        int ruinPieces = 0;
        if (ruinWalls)
        {
            const auto walls = loadModels(Ru::kWalls, std::size(Ru::kWalls));
            const auto tops = loadModels(Ru::kWallTops, std::size(Ru::kWallTops));
            const auto columns = loadModels(&Ru::kColumn, 1);
            const auto torches = loadModels(&Ru::kTorch, 1);
            if (!walls.empty())
            {
                const PropModel& w0 = walls[0];
                const float pw = (w0.hi.x - w0.lo.x) * w0.unit;   // 壁 1 枚の幅（x）と高さ
                const float ph = (w0.hi.y - w0.lo.y) * w0.unit;
                const float pd = (w0.hi.z - w0.lo.z) * w0.unit;
                const float halfW = W * 0.5f - kCs, halfD = D * 0.5f - kCs;
                struct Side { Vector3 n, t; float edge, half, yaw; };
                const Side sides[] = {
                    { { 0, 0, 1 }, { 1, 0, 0 }, halfD, halfW, 0.0f },
                    { { 0, 0, -1 }, { 1, 0, 0 }, halfD, halfW, 180.0f },
                    { { 1, 0, 0 }, { 0, 0, 1 }, halfW, halfD, 90.0f },
                    { { -1, 0, 0 }, { 0, 0, 1 }, halfW, halfD, 270.0f },
                };
                for (const Side& sd : sides)
                {
                    // 壁の中心は縁から厚みの半分だけ外（内側の面が縁に来る）。角の先まで 1 枚多く。
                    // 底は縁の地面の高さ、頂は平原（山頂の脇は山頂）+ wallHeight
                    const float out = sd.edge + pd * 0.5f;
                    for (float s = -sd.half - pw; s <= sd.half + pw; s += pw)
                    {
                        float lo, hi;
                        edgeSpan(sd.n, sd.t, sd.edge, s, pw * 0.5f, lo, hi);
                        const float wallH = (std::max)(hi, 0.0f) + cfg.wallHeight - lo;
                        const int rowCount = (std::max)(1, (int)std::ceil(wallH / (std::max)(ph, 0.5f)));
                        for (int r = 0; r < rowCount; ++r)
                        {
                            const bool top = (r == rowCount - 1) && !tops.empty();
                            const auto& list = top ? tops : walls;
                            const PropModel& pm = list[randi(0, (int)list.size() - 1)];
                            const Vector3 p = sd.n * out + sd.t * s;
                            spawnVisual(pm, 1.0f, sd.yaw, p.x, p.z, lo + r * ph);
                            ++ruinPieces;
                        }
                    }
                    if (!columns.empty())
                    {
                        const PropModel& cm = columns[0];
                        const float cd = (cm.hi.z - cm.lo.z) * cm.unit;
                        for (float s = -sd.half + 4.0f; s <= sd.half - 2.0f; s += 8.0f)
                        {
                            float lo, hi;
                            edgeSpan(sd.n, sd.t, sd.edge, s, 0.0f, lo, hi);
                            const Vector3 p = sd.n * (sd.edge - cd * 0.35f) + sd.t * s;
                            spawnVisual(cm, 1.0f, sd.yaw, p.x, p.z, lo);
                            ++ruinPieces;
                        }
                    }
                    if (!torches.empty())
                    {
                        const PropModel& tm = torches[0];
                        const float td = (tm.hi.z - tm.lo.z) * tm.unit;
                        for (float s = -sd.half + 8.0f; s <= sd.half - 2.0f; s += 12.0f)
                        {
                            float lo, hi;
                            edgeSpan(sd.n, sd.t, sd.edge, s, 0.0f, lo, hi);
                            const Vector3 p = sd.n * (sd.edge - td * 0.5f) + sd.t * s;
                            spawnVisual(tm, 1.0f, sd.yaw, p.x, p.z, lo + 2.0f);
                            if (outTorches)
                                outTorches->push_back(Vector3(p.x, lo + 2.7f, p.z) - sd.n * 0.4f);
                            ++ruinPieces;
                        }
                    }
                }
            }
        }

        // ---------- 洞の上の岩（低い山に見せる）と中の松明 ----------
        int caveRocks = 0, caveTorches = 0;
        if (cave)
        {
            // 岩の塊（坑 + 岩の壁）のマスと、塊の縁からの距離（4 近傍の BFS）。真ん中ほど高い岩を積む
            auto inMass = [&](int x, int z)
                {
                    if (x < 0 || z < 0 || x >= gw || z >= gd) return false;
                    const size_t i = (size_t)z * gw + x;
                    return zone[i] == kZoneMine || caveRing[i] != 0;
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
            const bool ownCliff = set.nCliff > 0;   // 遺跡は岩山の表が無いので森の岩を使う
            const auto roofRocks = loadModels(ownCliff ? set.cliff : F::kCliffRocks,
                ownCliff ? set.nCliff : std::size(F::kCliffRocks));
            for (int z = 1; z < gd - 1 && !roofRocks.empty(); z += 2)
                for (int x = 1; x < gw - 1; x += 2)
                {
                    const int gx = x + randi(0, 1), gz = z + randi(0, 1);   // 2x2 の区画の中でずらす
                    if (!inMass(gx, gz)) continue;
                    const PropModel& pm = roofRocks[randi(0, (int)roofRocks.size() - 1)];
                    const float h = (pm.hi.y - pm.lo.y) * pm.unit;
                    if (h < 0.1f) continue;
                    const float t = (maxDist > 0) ? (float)dist[(size_t)gz * gw + gx] / (float)maxDist : 0.0f;
                    const float targetH = (cfg.roofRockMin + (cfg.roofRockMax - cfg.roofRockMin) * t) * randf(0.8f, 1.2f);
                    const Vector3 c = grid.CellToWorld(gx, gz);
                    spawnVisual(pm, targetH / h, randf(0.0f, 360.0f), c.x + randf(-1.0f, 1.0f), c.z + randf(-1.0f, 1.0f),
                        roofTopY - targetH * 0.15f);   // 少し埋める
                    ++caveRocks;
                }

            // 松明：坑の底のマスで、隣が塞がった壁（岩の壁・外周）の所。壁の面に付け、caveTorchSpacing マス毎
            const auto torchModels = loadModels(&Ru::kTorch, 1);
            if (!torchModels.empty())
            {
                const PropModel& tm = torchModels[0];
                const float td = (tm.hi.z - tm.lo.z) * tm.unit;
                const float yaws[4] = { 90.0f, 270.0f, 0.0f, 180.0f };   // 壁の向き +x / -x / +z / -z（遺跡の壁と同じ取り方）
                const float gap = (float)(std::max)(cfg.caveTorchSpacing, 1) * kCs;
                std::vector<Vector3> placed;
                for (int z = 1; z < gd - 1; ++z)
                    for (int x = 1; x < gw - 1; ++x)
                    {
                        if (at(x, z) != kPit) continue;   // 底（坂・降り口は除く）
                        for (int k = 0; k < 4; ++k)
                        {
                            const int nx = x + ndx[k], nz = z + ndz[k];
                            if (zone[(size_t)nz * gw + nx] == kZoneMine || grid.IsWalkable(nx, nz)) continue;
                            const Vector3 d((float)ndx[k], 0.0f, (float)ndz[k]);
                            const Vector3 wallP = grid.CellToWorld(x, z) + d * (kCs * 0.5f);
                            bool farEnough = true;
                            for (const Vector3& q : placed)
                                if ((q - wallP).LengthSquared() < gap * gap) { farEnough = false; break; }
                            if (!farEnough) continue;
                            const Vector3 p = wallP - d * (td * 0.5f);
                            spawnVisual(tm, 1.0f, yaws[k], p.x, p.z, -mineD + 2.0f);
                            if (outTorches)
                                outTorches->push_back(Vector3(p.x, -mineD + 2.7f, p.z) - d * 0.4f);
                            placed.push_back(wallP);
                            ++caveTorches;
                            break;
                        }
                    }
            }
        }

        // ---------- 草を生やすマス ----------
        // 土の坂道・外周の崖・登れない台地（高さ場が 0 のまま = 箱の中に生えてしまう）・鉱洞・洞の岩の壁以外。
        // 崖の面そのものは GrassRenderer が高さ場の傾きで弾く
        if (outGrassMask)
        {
            auto& mask = *outGrassMask;
            mask.assign((size_t)gw * gd, 1);
            for (int z = 0; z < gd; ++z)
                for (int x = 0; x < gw; ++x)
                    if (x == 0 || z == 0 || x == gw - 1 || z == gd - 1 || at(x, z) == kRamp || at(x, z) == kMass
                        || zone[(size_t)z * gw + x] == kZoneMine)
                        mask[(size_t)z * gw + x] = 0;
            for (const auto& p : plateaus)
                if (!p.reachable)
                    for (int z = p.r.z; z < p.r.z + p.r.d; ++z)
                        for (int x = p.r.x; x < p.r.x + p.r.w; ++x)
                            mask[(size_t)z * gw + x] = 0;
        }

        // ---------- 三層の結果（箱・Boss の門の置き場所）----------
        if (outLayout)
        {
            *outLayout = Layout{};
            if (layers)
            {
                outLayout->summitRamps = summitRampInfo;
                outLayout->mineRamps = mineRampInfo;
                auto floorCell = [&](int x, int z)   // 鉱洞の底の歩けるマス（坂は除く）
                    {
                        return x >= 0 && z >= 0 && x < gw && z < gd && zone[(size_t)z * gw + x] == kZoneMine
                            && at(x, z) != kRamp && grid.IsWalkable(x, z);
                    };
                for (int z = 0; z < gd; ++z)
                    for (int x = 0; x < gw; ++x)
                    {
                        const int i = z * gw + x;
                        if (zone[(size_t)i] == kZoneSummit && at(x, z) != kSlope && grid.IsWalkable(x, z))
                            outLayout->summitCells.push_back(i);
                        if (floorCell(x, z)) outLayout->mineCells.push_back(i);
                    }
                // 一番奥：坂の降り口から底を 4 近傍で歩いた距離が一番大きく、周り 3x3 も底のマス
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
                    outLayout->hasMineDeep = true;
                    outLayout->mineDeep = grid.CellToWorld(best % gw, best / gw);
                    outLayout->mineDeep.y = -mineD;
                }
            }
        }

        if (layers)
            std::cout << "[Terrain] layers: summit " << std::count(zone.begin(), zone.end(), (uint8_t)kZoneSummit)
                << " cells +" << summitH << "m, " << summitRampsMade << " ramps / mine "
                << std::count(zone.begin(), zone.end(), (uint8_t)kZoneMine) << " cells -" << mineD << "m, "
                << mineRampsMade << " ramps, " << mineRocksPlaced << " rocks"
                << (cave ? ", roofed: " : "") << (cave ? std::to_string(caveRingCells) + " wall cells, "
                    + std::to_string(caveRocks) + " roof rocks, " + std::to_string(caveTorches) + " torches" : std::string())
                << std::endl;
        std::cout << "[Terrain] seed " << cfg.seed << ": " << terraces << " terraces (" << terraceTier2
            << " with a 2nd tier), " << plateaus.size() << " plateaus ("
            << tier2 << " with a 2nd tier, " << blocked << " without a ramp), "
            << rampCount << " ramps, " << treesPlaced << " trees (" << decorTrees << " decor), "
            << rocksPlaced << " rocks (" << decorRocks << " decor), " << bushesPlaced << " bushes, "
            << mountainRocks << " mountain rocks, " << ruinPieces << " ruin pieces, biome " << (int)cfg.biome << ", grid "
            << gw << "x" << gd << std::endl;

        // 木・岩の模型の大きさ（拡縮 1 倍、m）。「小さい物は見た目だけ」のしきい値を決める目安
        auto printSizes = [](const char* tag, const std::vector<PropModel>& list)
            {
                std::cout << "[Terrain] " << tag << " sizes (w x d x h m):";
                for (const auto& pm : list)
                {
                    const Vector3 s = (pm.hi - pm.lo) * pm.unit;
                    std::cout << " " << std::fixed << std::setprecision(2) << s.x << "x" << s.z << "x" << s.y;
                }
                std::cout << std::defaultfloat << std::endl;
            };
        printSizes("tree", trees);
        printSizes("bare tree", bareTrees);
        printSizes("rock", rocks);
    }
}
