// ============================================================
// TerrainBuild.cpp
// 地形を建てる側：配色・ノイズ、建てる出口（Emitter）、床のメッシュ・高さ場の衝突、
// 記録（MapData::Map）からの建て直し（TerrainGenerator::BuildFromMap）
// ============================================================
#include "World/TerrainBuild.h"
#include "Component/ModelComponent.h"
#include "Component/TransformComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Debug/TestSpawner.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include <functional>
#include <iostream>

using namespace TerrainBuild;

namespace TerrainBuild
{
    const Palette kPaletteGrass = {
        { 0.071f, 0.237f, 0.029f, 1 },   // (0.30, 0.52, 0.20)
        { 0.172f, 0.428f, 0.052f, 1 },   // (0.45, 0.68, 0.26)
        { 0.401f, 0.401f, 0.093f, 1 },   // (0.66, 0.66, 0.34) 所々の乾いた草
        { 0.133f, 0.374f, 0.047f, 1 },   // (0.40, 0.64, 0.25)
        { 0.268f, 0.172f, 0.099f, 1 },   // (0.55, 0.45, 0.35) 台地の側面（土と岩）
        { 0.325f, 0.290f, 0.247f, 1 },   // (0.60, 0.57, 0.53) 2 段目は灰色がかった岩
        { 0.486f, 0.325f, 0.148f, 1 },   // (0.72, 0.60, 0.42) 坂道は土の道（登り口が一目で分かる）
        { 0.172f, 0.148f, 0.133f, 1 },   // (0.45, 0.42, 0.40)
        { 0.106f, 0.076f, 0.052f, 1 },   // (0.36, 0.31, 0.26) 洞窟の底（湿った土）
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
        { 0.268f, 0.119f, 0.043f, 1 },   // (0.55, 0.38, 0.24) 洞窟の底（赤い砂岩）
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
        { 0.036f, 0.032f, 0.036f, 1 },   // (0.22, 0.21, 0.22) 洞窟の底（暗い岩）
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

    // 勾配ノイズ（Perlin 式、およそ -1..1）。値ノイズは格子点で傾きが 0 になり、丘が平らな所の継ぎ合わせに見えた
    // （起伏の傾きの中央値が 1°）。格子点ごとに乱数の向きの勾配を持たせ、5 次の補間でつなぐ
    float GradNoise(float x, float z, uint32_t seed)
    {
        const int ix = (int)std::floor(x), iz = (int)std::floor(z);
        const float fx = x - ix, fz = z - iz;
        auto dotGrad = [&](int gx, int gz, float dx, float dz)
            {
                const float a = Hash01(gx, gz, seed) * 6.2831853f;
                return std::cos(a) * dx + std::sin(a) * dz;
            };
        auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
        const float u = fade(fx), v = fade(fz);
        const float n00 = dotGrad(ix, iz, fx, fz), n10 = dotGrad(ix + 1, iz, fx - 1.0f, fz);
        const float n01 = dotGrad(ix, iz + 1, fx, fz - 1.0f), n11 = dotGrad(ix + 1, iz + 1, fx - 1.0f, fz - 1.0f);
        const float a = n00 + (n10 - n00) * u, b = n01 + (n11 - n01) * u;
        return (a + (b - a) * v) * 1.4142f;
    }

    // 洞窟の底：大きな湿りのむら + 細かい砂利
    Vector4 MineColor(float x, float z, uint32_t seed, const Palette& P)
    {
        const float big = ValueNoise(x / 14.0f, z / 14.0f, seed + 41u);
        const float fine = ValueNoise(x / 3.0f, z / 3.0f, seed + 43u);
        return LerpColor(P.mineDark, P.mineLight, std::clamp(big * 0.7f + fine * 0.45f - 0.15f, 0.0f, 1.0f));
    }

    Vector4 LerpColor(const Vector4& a, const Vector4& b, float t) { return a + (b - a) * t; }

    void BoxVerts(Vector3 v[8], const Vector3& lo, const Vector3& hi)
    {
        v[0] = { lo.x, lo.y, lo.z }; v[1] = { hi.x, lo.y, lo.z };
        v[2] = { hi.x, lo.y, hi.z }; v[3] = { lo.x, lo.y, hi.z };
        v[4] = { lo.x, hi.y, lo.z }; v[5] = { hi.x, hi.y, lo.z };
        v[6] = { hi.x, hi.y, hi.z }; v[7] = { lo.x, hi.y, hi.z };
    }

    namespace
    {
    // 静的な凸体の衝突だけ（世界座標の 8 頂点。見た目は無し）
    Entity SpawnHullCollider(Registry& reg, const Vector3 world[8], uint32_t layer)
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
        col.halfExtents = (hi - lo) * 0.5f;   // ブロードフェーズ用の包囲箱
        col.layer = layer;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.isStatic = true;
        rb.useGravity = false;
        reg.Add<RigidbodyComponent>(e, rb);
        return e;
    }
    }

    // ============================================================
    // Emitter
    // ============================================================
    Emitter::Emitter(Registry& reg, std::vector<Entity>& outTerrain, MapData::Map* rec)
        : m_Reg(reg), m_Out(outTerrain), m_Rec(rec)
    {
    }

    MapData::Tag Emitter::Begin(MapData::Kind kind)
    {
        uint32_t& next = m_Rec ? m_Rec->nextGroup : m_NextGroup;
        m_Tag.kind = (uint16_t)kind;
        m_Tag.group = next++;
        return m_Tag;
    }

    void Emitter::Box(const Vector3& lo, const Vector3& hi, uint32_t layer)
    {
        Entity e = TestSpawner::SpawnStaticBox(m_Reg, (lo + hi) * 0.5f, (hi - lo) * 0.5f);
        m_Reg.Get<ColliderComponent>(e).layer = layer;
        m_Out.push_back(e);
        if (m_Rec)
        {
            MapData::Box b;
            b.tag = m_Tag; b.lo = lo; b.hi = hi; b.layer = layer;
            m_Rec->boxes.push_back(b);
        }
    }

    void Emitter::Hull(const Vector3 world[8], uint32_t layer)
    {
        m_Out.push_back(SpawnHullCollider(m_Reg, world, layer));
        if (m_Rec)
        {
            MapData::Hull h;
            h.tag = m_Tag; h.layer = layer;
            for (int i = 0; i < 8; ++i) h.v[i] = world[i];
            m_Rec->hulls.push_back(h);
        }
    }

    void Emitter::Visual(const Vector3 world[8], const Vector4& top, const Vector4& side, int topLayer, int sideLayer)
    {
        m_Batch.SetLayers(topLayer, sideLayer);
        m_Batch.Append(world, top, side);
        m_Batch.SetLayers(-1, -1);
        if (m_Rec)
        {
            MapData::Visual v;
            v.tag = m_Tag; v.top = top; v.side = side; v.topLayer = topLayer; v.sideLayer = sideLayer;
            for (int i = 0; i < 8; ++i) v.v[i] = world[i];
            m_Rec->visuals.push_back(v);
        }
    }

    void Emitter::Prop(const std::string& path, const std::shared_ptr<Model>& model,
        const Vector3& pos, float yawDeg, float scale)
    {
        if (model)
        {
            Entity e = m_Reg.Create();
            TransformComponent tf;
            tf.position = pos;
            tf.rotation = { 0.0f, yawDeg, 0.0f };
            tf.scale = { scale, scale, scale };
            m_Reg.Add<TransformComponent>(e, tf);
            ModelComponent mc;
            mc.model = model;
            mc.batched = true;   // シーンの StaticPropRenderer がまとめて描く
            m_Reg.Add<ModelComponent>(e, mc);
            m_Out.push_back(e);
        }
        if (m_Rec)
        {
            MapData::Prop p;
            p.tag = m_Tag; p.model = m_Rec->ModelIndex(path); p.pos = pos; p.yawDeg = yawDeg; p.scale = scale;
            m_Rec->props.push_back(p);
        }
    }

    void Emitter::Block(GridWorld& grid, int x, int z, int w, int d)
    {
        grid.BlockArea(x, z, w, d);
        if (m_Rec)
        {
            MapData::Block b;
            b.tag = m_Tag; b.x = x; b.z = z; b.w = w; b.d = d;
            m_Rec->blocks.push_back(b);
        }
    }

    // 1 個ずつだと DrawMesh が数十回（影の 3 段でその 3 倍）。頂点は世界座標なので実体は原点
    void Emitter::FinishVisuals(ID3D11Device* device, TerrainGenerator::Biome biome, const float* refLum)
    {
        auto model = m_Batch.Build(device);
        if (!model) return;
        TerrainSurface::Get().Apply(device, *model, (int)biome, refLum);
        Entity e = m_Reg.Create();
        m_Reg.Add<TransformComponent>(e, TransformComponent{});
        ModelComponent mc;
        mc.model = model;
        m_Reg.Add<ModelComponent>(e, mc);
        m_Out.push_back(e);
    }

    // ============================================================
    // テクスチャの層毎の基準の明るさ
    // ============================================================
    void ComputeRefLum(uint32_t seed, TerrainGenerator::Biome biome, int gridW, float out[TerrainSurface::LayerCount])
    {
        auto lum = [](const Vector4& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
        const Palette& P = PaletteFor(biome);
        const float half = 0.5f * gridW * kCs;
        double ground = 0.0, mine = 0.0;
        constexpr int kN = 48;
        for (int iz = 0; iz < kN; ++iz)
            for (int ix = 0; ix < kN; ++ix)
            {
                const float x = -half + (ix + 0.5f) * (2.0f * half / kN);
                const float z = -half + (iz + 0.5f) * (2.0f * half / kN);
                ground += lum(TerrainGenerator::GroundColor(x, z, seed, biome));
                mine += lum(MineColor(x, z, seed, P));
            }
        for (int i = 0; i < TerrainSurface::LayerCount; ++i) out[i] = 0.0f;
        out[TerrainSurface::Ground] = (float)(ground / (kN * kN));
        out[TerrainSurface::Path] = lum(P.rampTop);
        out[TerrainSurface::Cliff] = 0.5f * (lum(P.cliff) + lum(P.cliffHigh));
        out[TerrainSurface::CaveFloor] = (float)(mine / (kN * kN));
        out[TerrainSurface::Rock] = lum(P.cliffHigh);
    }

    // ============================================================
    // 床の見た目（段々のメッシュ + 起伏）
    // マス毎の高さの段々のメッシュ 1 つ（平原・山頂の上面は値ノイズの色むら、洞窟の底は土と砂利、
    // 段の境は 2m 毎の縞の岩の壁）。台座が全部決まってから作る
    // ============================================================
    Entity SpawnGround(Registry& reg, ID3D11Device* device, const GridWorld& grid,
        const std::shared_ptr<ReliefField>& relief, const std::vector<uint8_t>& caveRing,
        const GroundParams& gp, const float* refLum)
    {
        const int gw = grid.Width(), gd = grid.Depth();
        const Palette& P = PaletteFor(gp.biome);
        const uint32_t s = gp.seed;
        const TerrainGenerator::Biome bm = gp.biome;
        const bool cave = gp.cave;
        const float rockLevel = gp.roofTopY;
        const Vector4 cliff = P.cliff, cliffHigh = P.cliffHigh;

        // 外周の崖のマスは内側の隣と同じ（フィールドの縁に段の壁を作らない）
        auto zoneAt = [&](int x, int z) { return relief->ZoneAtCell(x, z); };
        auto isRing = [&](int x, int z)
            {
                const int cx = std::clamp(x, 1, gw - 2), cz = std::clamp(z, 1, gd - 2);
                return caveRing[(size_t)cz * gw + cx] != 0;
            };
        auto levelAt = [&](int x, int z) -> float
            {
                if (isRing(x, z)) return gp.roofTopY;   // 洞の岩の壁（高さ場は 0 のまま、塞いだマス）
                const uint8_t id = zoneAt(x, z);
                return (id == ReliefField::kSummit) ? gp.summitH : (id == ReliefField::kMine) ? -gp.mineD : 0.0f;
            };
        auto topColor = [&P, s, bm, cave, rockLevel, cliffHigh](float x, float z, float level)
            {
                if (cave && std::fabs(level - rockLevel) < 0.01f)   // 洞の岩の壁の上
                {
                    const float k = 0.95f + 0.25f * ValueNoise(x / 5.0f, z / 5.0f, s + 51u);
                    return Vector4(cliffHigh.x * k, cliffHigh.y * k, cliffHigh.z * k, 1.0f);
                }
                return (level < -0.01f) ? MineColor(x, z, s, P) : TerrainGenerator::GroundColor(x, z, s, bm);
            };
        // 壁：山頂の崖・洞の岩は灰色がかった岩（2 段目の台地と同じ色）、洞窟の壁は土と岩。2m の帯ごとに明るさを変える（地層）
        auto wallColor = [s, cliff, cliffHigh](float x, float y, float z)
            {
                const Vector4& base = (y > 0.0f) ? cliffHigh : cliff;
                const float band = Hash01((int)std::floor(y / 2.0f), 17, s);
                const float drift = ValueNoise((x + z) / 9.0f, y / 4.0f, s + 31u);
                const float k = (0.82f + 0.3f * band) * (0.9f + 0.2f * drift);
                return Vector4(base.x * k, base.y * k, base.z * k, 1.0f);
            };
        // テクスチャの層（TerrainSurface）: 洞の岩の壁の上 = 岩、坑の底 = 洞の底、平原・山頂の上面 = 地面
        // （起伏の斜面は法線で自動にすると「道」の層になる）。
        // 壁は 高い側が岩の壁 か 低い側が坑 なら岩（坑の壁を y の境目で崖と岩に分けない）、他は自動（崖）
        auto topLayer = [&](int x, int z) -> int
            {
                if (isRing(x, z)) return TerrainSurface::Rock;
                if (zoneAt(x, z) == ReliefField::kMine) return TerrainSurface::CaveFloor;
                return gp.relief ? (int)TerrainSurface::Ground : -1;
            };
        auto wallLayer = [&](int hx, int hz, int lx, int lz) -> int
            {
                return (isRing(hx, hz) || zoneAt(lx, lz) == ReliefField::kMine) ? (int)TerrainSurface::Rock : -1;
            };
        // 頂点の高さ = そのマスの区域の起伏の面（洞の岩の壁は平らな roofTopY）
        auto heightAt = [&](int gx, int gz, float x, float z) -> float
            {
                if (isRing(gx, gz)) return gp.roofTopY;
                return relief->SurfaceIn(zoneAt(gx, gz), x, z);
            };

        Entity e = reg.Create();
        reg.Add<TransformComponent>(e, TransformComponent{});
        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateSteppedGrid(device, gw, gd, kCs, levelAt, topColor, wallColor, 2.0f,
            topLayer, wallLayer, gp.relief ? std::function<float(int, int, float, float)>(heightAt) : nullptr,
            gp.relief ? gp.reliefSubdiv : 1);
        if (mc.model) TerrainSurface::Get().Apply(device, *mc.model, (int)gp.biome, refLum);
        reg.Add<ModelComponent>(e, mc);
        return e;
    }

    // ============================================================
    // 地面の衝突（高さ場、2026-10-04）
    // 歩く面は relief（平原・山頂は起伏、洞窟は平ら）。崖の縦の壁は箱。Layer_Terrain（カメラの遮蔽・光線の先も見る）
    // ============================================================
    Entity SpawnHeightField(Registry& reg, const GridWorld& grid,
        const std::shared_ptr<ReliefField>& relief, float summitH)
    {
        const float W = grid.WorldWidth(), D = grid.WorldDepth();
        Entity e = reg.Create();
        TransformComponent tf;
        tf.position = Vector3(grid.OriginX() + W * 0.5f, 0.0f, grid.OriginZ() + D * 0.5f);
        reg.Add<TransformComponent>(e, tf);
        ColliderComponent col;
        col.shape = ColliderShape::HeightField;
        col.heightField = relief;
        col.halfExtents = Vector3(W * 0.5f, summitH + 20.0f, D * 0.5f);   // ブロードフェーズ用（フィールド全体 = 大きい物の表）
        col.layer = Layer_Terrain;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);
        RigidbodyComponent rb;
        rb.isStatic = true;
        rb.useGravity = false;
        reg.Add<RigidbodyComponent>(e, rb);
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

    // ============================================================
    // 記録からの建て直し（乱数なし）
    // 格子（通行・高さ場）と起伏は土台の配列をそのまま入れ、実体は記録を Emitter へ流し直す
    // ============================================================
    bool BuildFromMap(Registry& reg, ID3D11Device* device, GridWorld& grid, const MapData::Map& map,
        std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask,
        std::vector<DirectX::SimpleMath::Vector3>* outTorches, Layout* outLayout, MapData::Map* outMap, uint32_t parts)
    {
        if (map.gw != grid.Width() || map.gd != grid.Depth())
        {
            std::cout << "[Terrain] map is " << map.gw << "x" << map.gd << " but the grid is "
                << grid.Width() << "x" << grid.Depth() << std::endl;
            return false;
        }
        if (outMap == &map) outMap = nullptr;   // 同じ物へは書き戻さない
        if (outMap) outMap->Clear();

        const Biome biome = (Biome)map.biome;
        float refLum[TerrainSurface::LayerCount] = {};
        ComputeRefLum(map.seed, biome, map.gw, refLum);

        grid.SetData(map.walkable, map.heights);

        auto relief = std::make_shared<ReliefField>();
        relief->Init(grid, map.zone, map.summitH, map.mineD);
        relief->plain = map.reliefPlain;
        relief->summit = map.reliefSummit;

        std::vector<std::shared_ptr<Model>> models;
        models.reserve(map.models.size());
        int missing = 0;
        for (const auto& path : map.models)
        {
            models.push_back(ResourceManager::Get().LoadModel(path));
            if (!models.back()) ++missing;
        }

        Emitter emit(reg, outTerrain, outMap);
        const bool colliders = (parts & kPartColliders) != 0;
        if (colliders)
        for (const auto& b : map.boxes) { emit.SetTag(b.tag); emit.Box(b.lo, b.hi, b.layer); }
        if (colliders)
        for (const auto& h : map.hulls) { emit.SetTag(h.tag); emit.Hull(h.v, h.layer); }
        if (parts & kPartVisuals)
        for (const auto& v : map.visuals) { emit.SetTag(v.tag); emit.Visual(v.v, v.top, v.side, v.topLayer, v.sideLayer); }
        if (parts & kPartProps)
        for (const auto& p : map.props)
        {
            emit.SetTag(p.tag);
            emit.Prop(map.models[(size_t)p.model], models[(size_t)p.model], p.pos, p.yawDeg, p.scale);
        }
        for (const auto& b : map.blocks) { emit.SetTag(b.tag); emit.Block(grid, b.x, b.z, b.w, b.d); }

        GroundParams gp;
        gp.seed = map.seed; gp.biome = biome; gp.relief = map.relief; gp.reliefSubdiv = map.reliefSubdiv;
        gp.cave = map.cave; gp.roofTopY = map.roofTopY; gp.summitH = map.summitH; gp.mineD = map.mineD;
        if (parts & kPartGround) outTerrain.push_back(SpawnGround(reg, device, grid, relief, map.caveRing, gp, refLum));
        if (colliders) outTerrain.push_back(SpawnHeightField(reg, grid, relief, map.summitH));
        emit.FinishVisuals(device, biome, refLum);

        if (outGrassMask) *outGrassMask = map.grassMask;
        if (outTorches) *outTorches = map.torches;
        if (outLayout)
        {
            *outLayout = Layout{};
            outLayout->summitCells = map.summitCells;
            outLayout->mineCells = map.mineCells;
            outLayout->hasMineDeep = map.hasMineDeep;
            outLayout->mineDeep = map.mineDeep;
            for (const auto& r : map.summitRamps) outLayout->summitRamps.push_back({ r.top, r.down });
            for (const auto& r : map.mineRamps) outLayout->mineRamps.push_back({ r.top, r.down });
        }

        // 記録し直した物（Emitter が積んだ）に、土台と見出しを写す
        if (outMap)
        {
            MapData::Map& o = *outMap;
            o.seed = map.seed; o.biome = map.biome; o.gw = map.gw; o.gd = map.gd;
            o.relief = map.relief; o.reliefSubdiv = map.reliefSubdiv; o.cave = map.cave;
            o.summitH = map.summitH; o.mineD = map.mineD; o.roofTopY = map.roofTopY;
            o.zone = map.zone; o.caveRing = map.caveRing;
            o.reliefPlain = map.reliefPlain; o.reliefSummit = map.reliefSummit;
            o.walkable = map.walkable; o.heights = map.heights; o.grassMask = map.grassMask;
            o.torches = map.torches;
            o.summitCells = map.summitCells; o.mineCells = map.mineCells;
            o.hasMineDeep = map.hasMineDeep; o.mineDeep = map.mineDeep;
            o.summitRamps = map.summitRamps; o.mineRamps = map.mineRamps;
            o.nextGroup = map.nextGroup;
            o.placements = map.placements;
        }

        std::cout << "[Terrain] built from map: seed " << map.seed << " biome " << map.biome << ", "
            << map.boxes.size() << " boxes, " << map.hulls.size() << " hulls, " << map.visuals.size() << " visuals, "
            << map.props.size() << " props (" << missing << " models missing), " << map.blocks.size() << " blocks" << std::endl;
        return true;
    }
}
