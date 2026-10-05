// ============================================================
// TerrainGenerator.cpp
// ============================================================
#include "World/TerrainGenerator.h"
#include "World/TerrainBuild.h"
#include "World/GridWorld.h"
#include "ECS/Registry.h"
#include "Component/ModelComponent.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Debug/TestSpawner.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Renderer/TerrainSurface.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <climits>
#include <random>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

// 配色・ノイズ・起伏（ReliefField）・建てる出口（Emitter）は TerrainBuild（建てる側）にある
using namespace TerrainBuild;

namespace
{
    // 面（Biome）の色。Generate の頭で PaletteFor から写す（helper が既定引数で参照するので変数にしてある）
    Vector4 gPlateauTop, gCliff, gCliffHigh, gRampTop, gWallRock;
    Vector4 gGroundLight;   // 高台の坂の上面（草色）


    // 色を少しばらす（台地ごとに同じ緑にならないように）
    Vector4 Jitter(const Vector4& c, std::mt19937& rng, float amount)
    {
        std::uniform_real_distribution<float> j(-amount, amount);
        const float k = j(rng);
        return { std::clamp(c.x + k, 0.0f, 1.0f), std::clamp(c.y + k, 0.0f, 1.0f),
                 std::clamp(c.z + k * 0.5f, 0.0f, 1.0f), 1.0f };
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


    // 静的な箱（衝突は AABB）+ 見た目（上面と側面の 2 色）
    void SpawnBlock(Emitter& emit, const Vector3& lo, const Vector3& hi, const Vector4& top, const Vector4& side)
    {
        emit.Box(lo, hi, Layer_Terrain);
        Vector3 v[8];
        BoxVerts(v, lo, hi);
        emit.Visual(v, top, side);
    }



    // 起伏の素（台座で均す前）: 大きい丘（勾配ノイズ 2 段）+ 小さい土饅頭。面ごとの倍率（砂漠は砂丘で強め、遺跡は石畳なので弱め）。
    // ここで決めるのは「全体の設定」と「土饅頭（丘の部品）をどこへ撒くか」だけで、起伏に書くのは TerrainBuild の
    // BuildReliefNoise / AddHillsAndLimit（地図エディタが設定や丘の部品を変えた時も同じ関数で作り直す）
    void BuildRelief(ReliefField& f, const TerrainGenerator::Config& cfg, Emitter& emit,
        MapData::ReliefParams& rp, std::vector<MapData::Hill>& hills)
    {
        const float biomeMul = (cfg.biome == TerrainGenerator::Biome::Desert) ? 1.25f
            : (cfg.biome == TerrainGenerator::Biome::Dungeon) ? 0.5f : 1.0f;
        rp.hillHeight = cfg.hillHeight * biomeMul;
        rp.detailHeight = cfg.hillDetailHeight * biomeMul;
        rp.hillScale = cfg.hillScale;
        rp.detailScale = cfg.hillDetailScale;
        rp.summitMul = cfg.summitReliefMul;
        BuildReliefNoise(rp, cfg.seed, f.gw, f.gd, f.plain, f.summit);

        // 土饅頭: 半径 r、高さ h の (1 - (d/r)^2)^2。高さは r × bumpMaxSlope まで（斜面の最大の傾き ≒ 1.54 h / r）。
        // 構造物の配置の乱数と別の列（同じ seed で台地などの置き場所が起伏の有無で変わらない）
        std::mt19937 rng(cfg.seed ^ 0x9E3779B9u);
        auto randf = [&](float a, float b) { return std::uniform_real_distribution<float>(a, (std::max)(a, b))(rng); };
        const float W = f.gw * kCs, D = f.gd * kCs;
        const int count = (int)(cfg.bumpCount * (W * D) / (200.0f * 200.0f));
        hills.clear();
        for (int attempt = 0; attempt < count * 10 && (int)hills.size() < count; ++attempt)
        {
            const float cx = f.ox + randf(0.0f, W), cz = f.oz + randf(0.0f, D);
            const float r = randf(cfg.bumpRadiusMin, cfg.bumpRadiusMax);
            // 重ねない（重なると足し算で急になる。1 回目は 36° の所があった）
            bool overlap = false;
            for (const MapData::Hill& q : hills)
            {
                const float dx = q.x - cx, dz = q.z - cz, rr = q.radius + r;
                if (dx * dx + dz * dz < rr * rr) { overlap = true; break; }
            }
            if (overlap) continue;
            MapData::Hill hill;
            hill.x = cx; hill.z = cz; hill.radius = r;
            hill.height = (std::min)(randf(cfg.bumpHeightMin, cfg.bumpHeightMax), r * cfg.bumpMaxSlope) * biomeMul;
            hill.tag = emit.Begin(MapData::kHill);
            hills.push_back(hill);
        }

        // 丘の部品を足して、素の起伏の傾きを抑える（台座の戻りは後で別に抑える）
        AddHillsAndLimit(hills, rp.summitMul, cfg.reliefMaxSlopeDeg, f.gw, f.gd, f.plain, f.summit);
    }
}

namespace TerrainGenerator
{
    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask,
        std::vector<Vector3>* outTorches, Layout* outLayout, MapData::Map* outMap)
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
        if (outMap) outMap->Clear();
        Emitter emit(reg, outTerrain, outMap);
        std::vector<Vector3> torchList;   // 松明の位置（outTorches と記録へ）

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

        // テクスチャの層毎の「元の頂点色の基準の明るさ」（TerrainSurface。頂点色の明るさ / これ をテクスチャに薄く掛けて色むらを残す）。
        // 地面・洞の底は色の関数をフィールドに 48x48 点で平均、他はその層の配色
        float terrainRefLum[TerrainSurface::LayerCount] = {};
        ComputeRefLum(cfg.seed, cfg.biome, gw, terrainRefLum);
        // 遺跡は外周を岩山でなく壁にする（衝突の箱は同じ）
        const bool ruinWalls = cfg.biome == Biome::Dungeon;

        // 静的な箱（衝突だけ。見た目は別）。lo / hi は世界座標の隅
        auto solidBox = [&](const Vector3& lo, const Vector3& hi)
            {
                emit.Box(lo, hi, Layer_Terrain);
            };

        // ---------- フィールドの三層：隅の山頂と洞窟（2026-10-02）----------
        // zone: マス毎に 平原 / 山頂 / 洞窟。山頂と洞窟は対角の隅（どの組かは seed）。
        // 形 = 隅の正方形 → 内側の角の面取り → 内側の二辺に出っ張り 1〜2 個と凹み 1 個 → 1 マス幅の所を均す
        enum : uint8_t { kZonePlain = 0, kZoneSummit = 1, kZoneMine = 2 };
        std::vector<uint8_t> zone((size_t)gw * gd, kZonePlain);
        const bool layers = cfg.layers && gw >= 40 && gd >= 40;
        const float summitH = layers ? (std::max)(cfg.summitHeight, 1.0f) : 0.0f;
        const float mineD = layers ? (std::max)(cfg.mineDepth, 1.0f) : 0.0f;
        if (layers)
        {
            // 隅の局所座標 (u, v)：外周の崖の内側の隅のマス = (1, 1)、フィールドの内へ増える
            auto setLocal = [&](int corner, uint8_t id, int u0, int v0, int uw, int vw, bool on)
                {
                    for (int v = v0; v < v0 + vw; ++v)
                        for (int u = u0; u < u0 + uw; ++u)
                        {
                            const int gx = (corner & 1) ? (gw - 1 - u) : u;
                            const int gz = (corner & 2) ? (gd - 1 - v) : v;
                            if (gx < 1 || gz < 1 || gx >= gw - 1 || gz >= gd - 1) continue;
                            // 開始時の場所（中央）には掛けない
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
        // 洞窟の屋根（2026-10-03）：坑の周り 1 マスの岩の壁（caveRing、下り坂の口は除く）。見た目の高さは roofTopY。
        // 中身は下り坂を置いた後で決まる
        const bool cave = layers && cfg.mineRoof;
        const float roofBottomY = cave ? (std::max)(cfg.roofBottom, 2.5f) : 0.0f;
        const float roofTopY = cave ? (std::max)(cfg.roofTop, roofBottomY + 1.0f) : 0.0f;
        std::vector<uint8_t> caveRing((size_t)gw * gd, 0);

        // ---------- 起伏（2026-10-04）----------
        // relief = 平原・山頂の上面の起伏（ReliefField）。台座（padRect）で構造物の足元を均す。
        // 高さ場（雑魚・草・置物が見る）は「上げたマス（台地・坂・高台）と洞の岩の壁」以外を常に起伏の面と同じに保つ
        // （台座を当てる度に周りを書き直す）。上げたマスは RaiseRect / SpawnRamp の高さのまま
        static_assert(ReliefField::kPlain == kZonePlain && ReliefField::kSummit == kZoneSummit && ReliefField::kMine == kZoneMine,
            "ReliefField の区域の番号は zone と同じ");
        auto relief = std::make_shared<ReliefField>();
        relief->Init(grid, zone, summitH, mineD);
        MapData::ReliefParams reliefParams;   // 起伏の全体の設定と丘の部品（記録へ。地図エディタが変える）
        std::vector<MapData::Hill> hills;
        if (cfg.relief) BuildRelief(*relief, cfg, emit, reliefParams, hills);
        const int hsub = GridWorld::kHeightSub;
        constexpr float kPadSink = 0.5f;   // 台座に載せる箱の底を埋める深さ（台座の縁の起伏との隙間を見せない）
        std::vector<uint8_t> raised((size_t)gw * gd, 0);   // 台地・坂・高台が高さ場を上げたマス
        // 台座は順番に依らない混ぜ方にする（1 版目は「先に均した所を後の台座が触らない」鍵で、鍵の境に 0.5m で
        // 1m 以上の段（57°）ができた）。ノード毎に 面 = lerp(素の起伏, Lmix, 最大の重み)、
        // Lmix = Σ w^4 L / Σ w^4（芯（重み 1）の台座がほぼ勝つ = 構造物の足元はほぼ L のまま、境は連続）
        const std::vector<float> rawPlain = relief->plain, rawSummit = relief->summit;
        std::vector<MapData::Pad> pads;   // 台座（平原・山頂。芯（縁込み）のノードの範囲・戻しの幅・高さ）
        std::vector<float> mineW(relief->plain.size(), 0.0f);   // 洞窟の周りの 0 への均し（平原だけ）
        auto markRaised = [&](const Rect& r)
            {
                for (int z = (std::max)(r.z, 0); z < (std::min)(r.z + r.d, gd); ++z)
                    for (int x = (std::max)(r.x, 0); x < (std::min)(r.x + r.w, gw); ++x)
                        raised[(size_t)z * gw + x] = 1;
            };
        // 地形の部品（箱・坂）を記録して建てる。建て方は TerrainBuild::EmitBlockPart / EmitRampPart
        // （保存した地図を編集する MapEdit が、同じ関数で部品から衝突・見た目・高さ場を作り直す）
        //   bottom = 箱の底、base = 足元の高さ、raise = 上を歩ける（false なら格子を塞ぐ）、onGround = 地面に載る
        auto addBlock = [&](const Rect& r, float bottom, float top, float base, const Vector4& topColor,
            const Vector4& sideColor, bool raise, bool onGround)
            {
                MapData::BlockPart bp;
                bp.tag = emit.Tag();
                bp.x = r.x; bp.z = r.z; bp.w = r.w; bp.d = r.d;
                bp.bottom = bottom; bp.top = top; bp.base = base;
                bp.topColor = topColor; bp.sideColor = sideColor;
                bp.raise = raise; bp.onGround = onGround;
                if (outMap) outMap->blockParts.push_back(bp);
                EmitBlockPart(emit, &grid, grid, bp);
                if (raise) markRaised(r);
            };
        //   s = 下る向き、grassy = 草の坂（false = 土の道）、owner = 付いている箱（group の何番目。-1 = 無し）、offset = 辺に沿った位置
        auto addRamp = [&](const Rect& r, Side s, float base, float top, const Vector4& topColor, bool grassy,
            bool onGround, int owner, int offset)
            {
                MapData::RampPart rp;
                rp.tag = emit.Tag();
                rp.x = r.x; rp.z = r.z; rp.w = r.w; rp.d = r.d;
                rp.side = (uint8_t)s;
                rp.base = base; rp.top = top;
                rp.topColor = topColor; rp.sideColor = gCliff;
                rp.grassy = grassy; rp.onGround = onGround;
                rp.owner = owner; rp.offset = offset;
                if (outMap) outMap->rampParts.push_back(rp);
                EmitRampPart(emit, &grid, grid, rp);
                markRaised(r);
            };
        // 高さ場のマス（hx, hz の範囲、両端含む）を起伏の面へ合わせる。上げたマス・洞の岩の壁は触らない
        auto writeBase = [&](int hx0, int hz0, int hx1, int hz1)
            {
                hx0 = (std::max)(hx0, 0); hz0 = (std::max)(hz0, 0);
                hx1 = (std::min)(hx1, gw * hsub - 1); hz1 = (std::min)(hz1, gd * hsub - 1);
                for (int hz = hz0; hz <= hz1; ++hz)
                    for (int hx = hx0; hx <= hx1; ++hx)
                    {
                        const size_t c = (size_t)(hz / hsub) * gw + (hx / hsub);
                        if (raised[c] || caveRing[c]) continue;
                        const Vector3 p = grid.HeightCellToWorld(hx, hz);
                        grid.SetHeightExact(hx, hz, relief->SurfaceIn(relief->ZoneAtCell(hx / hsub, hz / hsub), p.x, p.z));
                    }
            };
        // 矩形（マス）のノードの起伏の平均（区域の基準からの差）
        auto avgRelief = [&](uint8_t zk, const Rect& r)
            {
                const auto& arr = (zk == kZoneSummit) ? relief->summit : relief->plain;
                double sum = 0.0;
                int n = 0;
                for (int iz = (std::max)(r.z * hsub, 0); iz <= (std::min)((r.z + r.d) * hsub, relief->nz - 1); ++iz)
                    for (int ix = (std::max)(r.x * hsub, 0); ix <= (std::min)((r.x + r.w) * hsub, relief->nx - 1); ++ix)
                    {
                        sum += arr[(size_t)iz * relief->nx + ix];
                        ++n;
                    }
                return n > 0 ? (float)(sum / n) : 0.0f;
            };
        // ノードの範囲の面を素の起伏と台座から作り直す（TerrainBuild::ComposeRelief。MapEdit も同じ関数で作り直す）
        auto rebuildRelief = [&](uint8_t zk, int ix0, int iz0, int ix1, int iz1)
            {
                ComposeRelief((zk == kZoneSummit) ? relief->summit : relief->plain,
                    (zk == kZoneSummit) ? rawSummit : rawPlain, pads, zk,
                    (zk == kZonePlain) ? &mineW : nullptr, relief->nx, relief->nz, ix0, iz0, ix1, iz1);
            };
        // 台座: 矩形（マス）の周り rim マスまでを高さ L（区域の基準からの差）に均し、そこから margin マスで元の起伏へ戻す。
        // 記録には今の札が付く（どの部品の台座か。部品を動かす時に作り直す）
        auto padRect = [&](uint8_t zk, const Rect& r, int rim, int margin, float L)
            {
                if (!cfg.relief) return;
                MapData::Pad p;
                p.tag = emit.Tag();
                p.zone = zk;
                p.ax0 = (r.x - rim) * hsub; p.ax1 = (r.x + r.w + rim) * hsub;
                p.az0 = (r.z - rim) * hsub; p.az1 = (r.z + r.d + rim) * hsub;
                p.m = (std::max)(margin, 1) * hsub;
                p.L = L;
                pads.push_back(p);
                rebuildRelief(zk, p.ax0 - p.m, p.az0 - p.m, p.ax1 + p.m, p.az1 + p.m);
                writeBase(p.ax0 - p.m - 1, p.az0 - p.m - 1, p.ax1 + p.m, p.az1 + p.m);
            };

        // 洞窟の周り（岩の壁・洞の口・下り坂の上端）は平原の 0 に均す: 坑のマスからの距離（ノード、チェビシェフ）が
        // 4 マス以内は 0、そこから padMargin マスで元の起伏へ
        if (cfg.relief && layers)
        {
            ComputeMineWeights(zone, gw, gd, cfg.padMargin, mineW);
            rebuildRelief(kZonePlain, 0, 0, relief->nx - 1, relief->nz - 1);
        }

        // マス毎の地面の高さ（見た目）。外周の崖のマスは内側の隣と同じ（フィールドの縁に段の壁を作らない）
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

        // ---------- 床（草地）----------
        // 見た目: マス毎の高さの段々のメッシュ 1 つ（平原・山頂の上面は値ノイズの色むら、洞窟の底は土と砂利、
        //   段の境は 2m 毎の縞の岩の壁）。
        // 衝突: 洞窟以外は上面 y = 0 の箱（底は洞窟の底より下 = 洞窟の壁にもなる）、洞窟の底は上面 -mineDepth、
        //   山頂は 0〜summitHeight。どれもマスの印を矩形に分けた箱の組。
        // 高さ場: 山頂は上げる、洞窟はそのまま -mineDepth を書く（坂は後で高い方を書く）。
        // 床は格子に登録しない（上を歩くものなので通行を塞がない）
        float floorPlainTop = 0.0f, floorSummitTop = 0.0f;   // 床の箱の上面（記録へ。区域を塗り替えた時に同じ高さで作り直す）
        const float floorBottom = -mineD - 1.0f;
        {
            // 起伏（2026-10-04）: 歩く面は高さ場の衝突（下の relief）。箱は上面を起伏の一番低い所より下げ、
            // 崖（山頂の縁・坑の壁）の縦の壁としてだけ残す。山頂の箱は底を floorBottom まで伸ばす
            // （周りの平原が 0 より低い所で山頂の下に潜り込めないように）
            float plainTop = 0.0f, summitTop = summitH;
            if (cfg.relief)
            {
                plainTop = *std::min_element(relief->plain.begin(), relief->plain.end()) - 0.1f;
                summitTop = summitH + *std::min_element(relief->summit.begin(), relief->summit.end()) - 0.1f;
            }
            emit.Begin(MapData::kFloor);
            EmitFloorBoxes(emit, grid, zone, floorBottom, plainTop, summitTop, mineD);
            floorPlainTop = plainTop; floorSummitTop = summitTop;
            // 高さ場 = 起伏の面（山頂は +summitH、洞窟は -mineD）。外周の崖のマス（塞いだマス）も内側の隣と同じ区域の面
            // （2026-10-03。外周の岩を少し外へ下げてこのマスの地面が見え、草を生やす）
            writeBase(0, 0, gw * hsub - 1, gd * hsub - 1);
            // 見た目のメッシュと場外の地面は台座が全部決まってから（高台・台地の後）
        }

        // ---------- 外周の崖 ----------
        // 1 マス幅で四辺を囲む。場外へ出る・落ちるをここで殺す（格子も塞ぐ）。
        // 高さは洞窟の底の下から山頂 + wallHeight まで（山頂から外へ飛び出せない）。
        // 岩山にする時は衝突の箱だけ（見た目は後で岩を積む）
        auto wall = [&](int x, int z, int w, int d)
            {
                const Vector3 lo = RectMin(grid, { x, z, w, d }) + Vector3(0.0f, floorBottom - 1.0f, 0.0f);
                const Vector3 hi = RectMin(grid, { x, z, w, d }) + Vector3(w * kCs, summitH + cfg.wallHeight, d * kCs);
                if (cfg.rockMountains || ruinWalls)
                    emit.Box(lo, hi, Layer_Terrain);
                else
                    SpawnBlock(emit, lo, hi, gPlateauTop, gWallRock);
                emit.Block(&grid, x, z, w, d);
            };
        emit.Begin(MapData::kOuterWall);
        wall(0, 0, gw, 1);
        wall(0, gd - 1, gw, 1);
        wall(0, 1, 1, gd - 2);
        wall(gw - 1, 1, 1, gd - 2);

        // ---------- 占有マップ ----------
        // 空き地 / 台地・高台の上面 / 坂道 / 初期地点 / 坂道の降り口（空き地のまま残す）/ 高台の長い坂 /
        // 高台の坂の麓（地面のまま。木・岩・台地は置かない。茂み・草は置く）/ 高台の上の通り道（木・岩を置かない）/
        // 山頂の上面（木・岩を置く）/ 洞窟の底（岩だけ置く）
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

        // プレイヤーの初期地点（フィールド中央）の周りは平らに空ける
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

        // ---------- 山頂の長い坂・洞窟の下り坂 ----------
        // 区域の縁のうち、幅 w マスが真っ直ぐ同じ向きに平原へ面している所に、縁と直角の坂を置く。
        //   山頂: 坂は縁の外（平原）へ下る。麓の先 terraceClear マスは空き地（kApron）、坂の上端の山頂 2 マスは通り道（kWay）。
        //   洞窟: 坂は縁の内（底）へ下る（坂の両脇は底へ飛び降りられる）。入口の平原側 3 マスと底の降り口 1 マスは空ける（kLanding）。
        // 候補を全部集めて混ぜ、互いに離れた物から本数分取る
        int summitRampsMade = 0, mineRampsMade = 0;
        std::vector<Rect> mineLandings;   // 洞窟の坂の降り口（底側）。一番奥を探す起点
        std::vector<Rect> mineMouths;     // 洞窟の坂の上端の外 1 列（平原側）= 洞の口
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
                    // 起伏: 坂と麓を麓の起伏の平均の高さに、坂の上端の山頂側（通り道）を山頂の基準の高さに均す
                    const float foot = cfg.relief ? avgRelief(kZonePlain, c.apron) : 0.0f;
                    emit.Begin(MapData::kSummitRamp);   // 台座の記録にも同じ札を付けるので、均す前に振る
                    padRect(kZonePlain, c.ramp, 1, cfg.padMargin, foot);
                    padRect(kZonePlain, c.apron, 0, cfg.padMargin, foot);
                    padRect(kZoneSummit, c.extra, 1, cfg.padMargin, 0.0f);
                    addRamp(c.ramp, c.s, foot, summitH, Jitter(gGroundLight, rng, 0.02f), true, true, -1, 0);
                    summitRampInfo.push_back(rampInfo(c.ramp, c.s, summitH));
                    made.push_back(c.ramp);
                }
                summitRampsMade = (int)made.size();
            }

            // ---- 洞窟 ----
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
                            // 坂・降り口は洞窟の底（kPit = 洞窟の中だけ）、入口は平原の空き地
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
                    emit.Begin(MapData::kMineRamp);
                    addRamp(c.ramp, Opposite(c.s), -mineD, 0.0f, gRampTop, false, false, -1, 0);
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

        // ---------- 洞窟の屋根（2026-10-03、ユーザー：推奨どおり）----------
        // 坑の上を岩の塊で覆い、外から見ると山の麓の洞窟にする。
        //   岩の壁 = 坑の 8 近傍の平原のマス（下り坂の口を除く）。格子を塞ぎ（雑魚は縁から飛び降りられない。
        //     出入りは下り坂だけ）、見た目は roofTop の高さの段（段々のメッシュが外の壁・中の壁を描く）。
        //   屋根 = 坑の上の roofBottom〜roofTop の板、口の上 = 同じ高さの梁（口の高さは roofBottom）。
        //   衝突は壁・屋根・梁とも roofCollisionTop まで（見えない。上に乗れない）
        int caveRingCells = 0;
        if (cave)
        {
            emit.Begin(MapData::kCaveRoof);
            // 岩の壁（坑の 8 近傍、口を除く）→ 塞ぐ・壁と屋根と梁の衝突・屋根の見た目（TerrainBuild。区域を塗り替えた時も同じ関数）
            std::vector<uint8_t> mouth;
            ComputeCaveRing(zone, mineMouths, gw, gd, caveRing, mouth);
            for (int z = 1; z < gd - 1; ++z)
                for (int x = 1; x < gw - 1; ++x)
                    if (caveRing[(size_t)z * gw + x]) { at(x, z) = kMass; ++caveRingCells; }
            const float collTop = (std::max)(cfg.roofCollisionTop, roofTopY);
            EmitCaveRoof(emit, &grid, grid, zone, caveRing, mouth, roofBottomY, roofTopY, collTop, gCliffHigh);

            // 岩の壁のマスの高さ場（2026-10-03、通し検査 soak で見つけた）。歩けないので誰も立たないが、
            // 隣の歩けるマスの双線形の高さに混ざる。平原の 0 のままだと、坑の縁に押し付けられた雑魚が
            // 壁の途中（坑の底から 3.8m）まで持ち上がって見えた。高さマス毎に、一番近い歩けるマス
            // （壁ではない）の高さにする = 坑側の半分は坑の底、外側の半分は平原の高さ
            {
                constexpr int kSub = GridWorld::kHeightSub;
                std::vector<std::pair<int, float>> fill;   // (高さマスの番号, 高さ)
                for (int z = 0; z < gd; ++z)
                    for (int x = 0; x < gw; ++x)
                    {
                        if (!caveRing[(size_t)z * gw + x]) continue;
                        for (int sz = 0; sz < kSub; ++sz)
                            for (int sx = 0; sx < kSub; ++sx)
                            {
                                const int hx = x * kSub + sx, hz = z * kSub + sz;
                                int best = INT_MAX;
                                float bh = grid.HeightAt(hx, hz);
                                for (int r = 1; r <= 2 * kSub && best > r * r; ++r)
                                    for (int dz = -r; dz <= r; ++dz)
                                        for (int dx = -r; dx <= r; ++dx)
                                        {
                                            if ((std::max)(std::abs(dx), std::abs(dz)) != r) continue;
                                            const int nx = hx + dx, nz = hz + dz;
                                            const int cx = nx / kSub, cz = nz / kSub;
                                            if (nx < 0 || nz < 0 || cx >= gw || cz >= gd) continue;
                                            if (!grid.IsWalkable(cx, cz) || caveRing[(size_t)cz * gw + cx]) continue;
                                            const int d2 = dx * dx + dz * dz;
                                            if (d2 < best) { best = d2; bh = grid.HeightAt(nx, nz); }
                                        }
                                fill.push_back({ hz * gw * kSub + hx, bh });
                            }
                    }
                for (const auto& f : fill)
                    grid.SetHeightExact(f.first % (gw * kSub), f.first / (gw * kSub), f.second);
            }
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

            // 起伏: 高台・坂・麓を足元の起伏の平均の高さ base に均し、全部 base に載せる（箱の底は少し埋める）
            const float base = cfg.relief ? 0.5f * (avgRelief(kZonePlain, p) + avgRelief(kZonePlain, sr)) : 0.0f;
            emit.Begin(MapData::kTerrace);   // 台座の記録にも同じ札を付けるので、均す前に振る
            padRect(kZonePlain, p, 1, cfg.padMargin, base);
            padRect(kZonePlain, sr, 1, cfg.padMargin, base);
            padRect(kZonePlain, apron, 0, cfg.padMargin, base);
            addBlock(p, base - kPadSink, base + h1, base,
                Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliff, rng, 0.015f), true, true);
            addRamp(sr, s, base, base + h1, Jitter(gGroundLight, rng, 0.02f), true, true, 0, 0);

            if (tier2)
            {
                const int w2 = randi(3, across - 2);
                const int v0 = randi(1, across - 1 - w2);
                const Rect t = LocalRect(p, s, 2 + run2, top2, v0, w2);
                const Rect s2 = LocalRect(p, s, 2, run2, v0, w2);
                addBlock(t, base + h1, base + h1 + h2, base + h1,
                    Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliffHigh, rng, 0.015f), true, false);
                addRamp(s2, s, base + h1, base + h1 + h2, Jitter(gGroundLight, rng, 0.02f), true, false, 1, 0);   // 2 段目の幅いっぱい
                mark(s2, kSlope);
                ++terraceTier2;
            }
            ++terraces;
        }

        // ---------- 1 段目の台地（場所だけ先に決める）----------
        // base = 足元（台座）の高さ。tag = 記録の札（坂道・本体・2 段目を同じ 1 個にまとめる）
        struct Plateau { Rect r; float top; bool reachable; float base; MapData::Tag tag; };
        std::vector<Plateau> plateaus;
        for (int attempt = 0; attempt < cfg.plateauCount * 30 && (int)plateaus.size() < cfg.plateauCount; ++attempt)
        {
            Rect r = { 0, 0, randi(cfg.plateauMin, cfg.plateauMax), randi(cfg.plateauMin, cfg.plateauMax) };
            r.x = randi(2, gw - 2 - r.w);
            r.z = randi(2, gd - 2 - r.d);
            // 間隔 gap を空ける = 台地同士の間に地上の通路が残る
            if (!inside(r) || !isFree(r, cfg.gap)) continue;
            mark(r, kPlateau);
            // 起伏: 足元を起伏の平均の高さに均す（坂道は付けた時に同じ高さへ）
            const float base = cfg.relief ? avgRelief(kZonePlain, r) : 0.0f;
            const MapData::Tag tag = emit.Begin(MapData::kPlateau);   // 台座の記録にも同じ札
            padRect(kZonePlain, r, 1, cfg.padMargin, base);
            const float top = base + randf(cfg.heightMin, cfg.heightMax);
            plateaus.push_back({ r, top, false, base, tag });
        }

        // ---------- 坂道（1 段目）。1〜2 本、降り口が地面に続く所だけ ----------
        auto placeRamp = [&](const Rect& p, Side s, float base, float top, bool onGround) -> bool
            {
                const int sideLen = SideLength(p, s);
                if (sideLen < cfg.rampWidth) return false;
                const int len = rampLen(top - base);
                const int offset = randi(0, sideLen - cfg.rampWidth);
                const Rect rr = RampRect(p, s, offset, cfg.rampWidth, len);
                const Rect land = LandingRect(rr, s);
                if (onGround && (!inside(rr) || !isFree(rr, 0) || !inside(land) || !isGround(land)))
                    return false;
                // 2 段目の坂道（1 段目の上）も載せる：置物で登り口を塞がないように
                mark(rr, kRamp);
                mark(land, kLanding);
                if (onGround)   // 起伏: 坂と降り口を台地の足元と同じ高さに均す
                {
                    padRect(kZonePlain, rr, 0, cfg.padMargin, base);
                    padRect(kZonePlain, land, 0, cfg.padMargin, base);
                }
                // 付いている箱：地面の坂は台地の本体（group の 0 番目の箱）、2 段目の坂は 2 段目（1 番目）
                addRamp(rr, s, base, top, gRampTop, false, onGround, onGround ? 0 : 1, offset);
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
            emit.SetTag(p.tag);
            for (Side s : order)
                if (made < want && placeRamp(p.r, s, p.base, p.top, true)) ++made;
            p.reachable = (made > 0);
            rampCount += made;
        }

        // ---------- 台地の本体 ----------
        // 坂道が 1 本も付かなかった台地は登れない：格子を塞いで上に何も湧かないようにする
        for (const auto& p : plateaus)
        {
            emit.SetTag(p.tag);
            addBlock(p.r, p.base - kPadSink, p.top, p.base,
                Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliff, rng, 0.015f), p.reachable, true);
            if (!p.reachable) ++blocked;
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
            emit.SetTag(p.tag);
            addBlock(t, p.top, top, p.top,
                Jitter(gPlateauTop, rng, 0.02f), Jitter(gCliffHigh, rng, 0.015f), true, false);
            placeRamp(t, s, p.top, top, false);
            ++rampCount;
            ++tier2;
        }

        // ---------- 起伏の仕上げ：台座の戻りなどの急な所を抑える（2026-10-05）----------
        // 台座の芯（重み 1 = 構造物の足元・洞窟の周り）は動かさない。抑えた後の面で高さ場を書き直す
        if (cfg.relief)
        {
            LimitReliefSlopes(relief->plain, pads, kZonePlain, &mineW, relief->nx, relief->nz, relief->step, cfg.reliefMaxSlopeDeg);
            LimitReliefSlopes(relief->summit, pads, kZoneSummit, nullptr, relief->nx, relief->nz, relief->step, cfg.reliefMaxSlopeDeg);
            writeBase(0, 0, gw * hsub - 1, gd * hsub - 1);
        }

        // ---------- 床の見た目（段々のメッシュ + 起伏）----------
        // 台座が全部決まってから作る（2026-10-04 に高台・台地の後へ移した）。中身は TerrainBuild::SpawnGround
        GroundParams groundParams;
        groundParams.seed = cfg.seed; groundParams.biome = cfg.biome;
        groundParams.relief = cfg.relief; groundParams.reliefSubdiv = cfg.reliefSubdiv;
        groundParams.cave = cave; groundParams.roofTopY = roofTopY;
        groundParams.summitH = summitH; groundParams.mineD = mineD;
        outTerrain.push_back(SpawnGround(reg, device, grid, relief, caveRing, groundParams, terrainRefLum));

        // ---------- フィールドの外へ 40m の地面 ----------
        // 見た目だけ（外周の岩の隙間から下の空が見えないように）。外周のマス毎に外向きの帯、四隅は正方形。
        // 高さは縁の地面（起伏の面）
        {
            const Palette& P = PaletteFor(cfg.biome);
            const Vector4 skirtTop = LerpColor(P.groundDark, P.groundLight, 0.4f);
            constexpr float kSkirt = 40.0f;
            emit.Begin(MapData::kSkirt);
            const float ox = grid.OriginX(), oz = grid.OriginZ();
            auto skirtLevel = [&](float x, float z)
                {
                    return relief->Height(std::clamp(x, ox + 0.01f, ox + W - 0.01f), std::clamp(z, oz + 0.01f, oz + D - 0.01f));
                };
            auto skirtBox = [&](float x0, float z0, float x1, float z1, float lv)
                {
                    Vector3 v[8];
                    BoxVerts(v, { x0, lv - 2.0f, z0 }, { x1, lv, z1 });
                    emit.Visual(v, skirtTop, gCliff);
                };
            for (int i = 0; i < gw; ++i)   // 南（-z）と北（+z）
            {
                const float x0 = ox + i * kCs, x1 = x0 + kCs, xm = x0 + kCs * 0.5f;
                skirtBox(x0, oz - kSkirt, x1, oz, skirtLevel(xm, oz));
                skirtBox(x0, oz + D, x1, oz + D + kSkirt, skirtLevel(xm, oz + D));
            }
            for (int j = 0; j < gd; ++j)   // 西（-x）と東（+x）
            {
                const float z0 = oz + j * kCs, z1 = z0 + kCs, zm = z0 + kCs * 0.5f;
                skirtBox(ox - kSkirt, z0, ox, z1, skirtLevel(ox, zm));
                skirtBox(ox + W, z0, ox + W + kSkirt, z1, skirtLevel(ox + W, zm));
            }
            skirtBox(ox - kSkirt, oz - kSkirt, ox, oz, skirtLevel(ox, oz));
            skirtBox(ox + W, oz - kSkirt, ox + W + kSkirt, oz, skirtLevel(ox + W, oz));
            skirtBox(ox - kSkirt, oz + D, ox, oz + D + kSkirt, skirtLevel(ox, oz + D));
            skirtBox(ox + W, oz + D, ox + W + kSkirt, oz + D + kSkirt, skirtLevel(ox + W, oz + D));
        }

        // ---------- 地面の衝突（高さ場、2026-10-04）と、積んだ見た目（外周・台地・坂道・高台）の 1 つのモデル ----------
        outTerrain.push_back(SpawnHeightField(reg, grid, relief, summitH));
        emit.FinishVisuals(device, cfg.biome, terrainRefLum);

        // ---------- 自然物（KayKit Forest）----------
        // 木と岩（置物）: 足跡のマス + 周り 1 マスが「歩ける・同じ高さ・空き地か台地の上・他の置物無し」の所だけ。
        //   坂道・降り口・台地の縁（周りの高さが違う）には置かないので、登り口は塞がれない。
        //   足跡は格子で塞ぐ（雑魚は避けて通る。GPU の弾もそのマスで当たる）。
        //   衝突は別の実体の箱（Layer_Prop：プレイヤーはぶつかるが、カメラの遮蔽判定は見ない）。
        // 茂みと草: 見た目だけ。坂道の上以外ならどこでも（初期地点にも生える）
        struct PropModel { std::shared_ptr<Model> model; float unit; Vector3 lo, hi; const char* path; };
        auto loadModels = [](const char* const* paths, size_t n)
            {
                std::vector<PropModel> out;
                for (size_t i = 0; i < n; ++i)
                {
                    auto m = ResourceManager::Get().LoadModel(paths[i]);
                    if (m) out.push_back({ m, m->GetFileUnitScale(), m->GetBoundsMin(), m->GetBoundsMax(), paths[i] });
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
        // 置物 1 個の始まり = 新しい group（この後に出す衝突・塞ぐマスも同じ 1 個に入る）
        auto spawnVisual = [&](const PropModel& pm, float scale, float yawDeg, float x, float z, float ground,
            MapData::Kind kind)
            {
                const float u = pm.unit * scale;
                emit.Begin(kind);
                emit.Prop(pm.path, pm.model, { x, ground - pm.lo.y * u, z }, yawDeg, u);
            };
        // 衝突だけの実体（軸平行の箱）
        auto spawnPropCollider = [&](const Vector3& center, const Vector3& half)
            {
                emit.Box(center - half, center + half, Layer_Prop);
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
                const float h0 = cellHeight(cx0, cz0);
                // 起伏があると隣のマスと 2m で最大 0.9m ほど違う。台地の縁（段差は 2.5m 以上）は引き続き弾く
                const float maxStep = cfg.relief ? 0.7f : 0.2f;
                for (int zz = gz0 - 1; zz <= gz1 + 1; ++zz)
                    for (int xx = gx0 - 1; xx <= gx1 + 1; ++xx)
                    {
                        if (xx < 1 || zz < 1 || xx >= gw - 1 || zz >= gd - 1) return false;
                        // 空き地・台地・山頂の上面。洞窟の底は岩だけ
                        const uint8_t o = at(xx, zz);
                        const bool okHere = o == kFree || o == kPlateau || o == kHigh || (!isTree && o == kPit);
                        if (!okHere || !grid.IsWalkable(xx, zz) || propAt[(size_t)zz * gw + xx]) return false;
                        if (std::fabs(cellHeight(xx, zz) - h0) > maxStep) return false;
                    }

                for (int zz = gz0; zz <= gz1; ++zz)
                    for (int xx = gx0; xx <= gx1; ++xx)
                        propAt[(size_t)zz * gw + xx] = 1;

                // 底 = 足跡（木は幹）の四隅と中心の一番低い所（斜面の下側で浮かない。上側は埋まる）
                float h = grid.SampleHeight(x, z);
                {
                    const float fx = isTree ? 0.35f : hx * 0.8f, fz = isTree ? 0.35f : hz * 0.8f;
                    for (int k = 0; k < 4; ++k)
                        h = (std::min)(h, grid.SampleHeight(x + ((k & 1) ? fx : -fx), z + ((k & 2) ? fz : -fz)));
                }
                spawnVisual(pm, scale, yawDeg, x, z, h, isTree ? MapData::kTree : MapData::kRock);
                if (!blocks)
                {
                    ++(isTree ? decorTrees : decorRocks);
                    return true;
                }
                emit.Block(&grid, gx0, gz0, gx1 - gx0 + 1, gz1 - gz0 + 1);
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

        // 洞窟の底の岩（崩れた岩屑。木は生やさない）。底のマスから選ぶ
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
        // 草はモデルではなく GrassRenderer が GPU で生やす（下の outGrassMask）
        int bushesPlaced = 0;
        for (int attempt = 0; attempt < bushTarget * 10 && bushesPlaced < bushTarget && !bushes.empty(); ++attempt)
        {
            float x, z;
            randomPoint(x, z);
            if (randf(0.0f, 1.0f) < 0.5f && ValueNoise(x / 26.0f, z / 26.0f, cfg.seed + 21u) <= 0.58f) continue;
            int gx, gz;
            grid.WorldToCell({ x, 0.0f, z }, gx, gz);
            if (!grid.IsWalkable(gx, gz) || at(gx, gz) == kRamp || zoneAt(gx, gz) == kZoneMine) continue;
            // 段差（台地の縁・坂の脇）は弾く。起伏の斜面（0.6m で 0.3m 程まで）は置いて、一番低い所に合わせる
            const float tol = cfg.relief ? 0.35f : 0.1f;
            const float h0 = grid.SampleHeight(x, z);
            float h = h0;
            bool stepped = false;
            for (int k = 0; k < 4 && !stepped; ++k)
            {
                const float s = grid.SampleHeight(x + (k == 0 ? 0.6f : k == 1 ? -0.6f : 0.0f), z + (k == 2 ? 0.6f : k == 3 ? -0.6f : 0.0f));
                stepped = std::fabs(s - h0) > tol;
                h = (std::min)(h, s);
            }
            if (stepped) continue;
            spawnVisual(bushes[randi(0, (int)bushes.size() - 1)], randf(0.8f, 1.3f), randf(0.0f, 360.0f), x, z, h, MapData::kBush);
            ++bushesPlaced;
        }

        // フィールドの縁の地面の高さ（外周の岩山・遺跡の壁の底と頂を決める）。
        // n = 外向きの法線、t = 辺に沿う向き、edge = 縁までの距離。辺に沿った s ± halfLen の 3 点の最低 lo / 最高 hi
        // （山頂の脇は高く、洞窟の脇は低い。段の境に掛かる物は低い方から高い方まで届かせる）
        auto edgeSpan = [&](const Vector3& n, const Vector3& t, float edge, float s, float halfLen, float& lo, float& hi)
            {
                lo = 1e9f; hi = -1e9f;
                const float limX = W * 0.5f - kCs * 1.5f, limZ = D * 0.5f - kCs * 1.5f;   // 縁の内側のマスの中
                for (int k = -1; k <= 1; ++k)
                {
                    const Vector3 p = n * (edge - kCs * 0.5f) + t * (s + k * halfLen);
                    const float h = relief->Height(std::clamp(p.x, -limX, limX), std::clamp(p.z, -limZ, limZ));   // 起伏の面（区域の式）
                    lo = (std::min)(lo, h);
                    hi = (std::max)(hi, h);
                }
            };

        // ---------- 外周の岩山（rockMountains）----------
        // 大きい岩を拡大して、崖の箱の外側へ 3 列に隙間なく積む（見た目だけ。衝突・格子は崖の箱）。
        // 手前の列は内側の面がフィールドの縁（崖のマスの内側）に来るように置き、奥ほど高く。
        // 四辺とも角の先まで伸ばして、角に穴が開かないようにする。
        // 底は縁の地面の高さ（洞窟の脇は洞窟の底から）、高さは平原（山頂の脇は山頂）から測る
        int mountainRocks = 0;
        int edgeRockColliders = 0, edgeRockCells = 0;   // 一番手前の列の衝突の数・塞いだマス
        if (cfg.rockMountains && !ruinWalls && set.nCliff > 0)
        {
            const auto cliffRocks = loadModels(set.cliff, set.nCliff);
            struct Row { float offset, step, hMin, hMax; };
            const Row rows[] = {
                { 0.0f, 4.5f, 8.0f, 13.0f },
                { 7.0f, 6.0f, 15.0f, 22.0f },
                { 16.0f, 8.0f, 24.0f, 34.0f },
            };
            const float halfW = W * 0.5f - kCs, halfD = D * 0.5f - kCs;   // フィールドの縁（崖のマスの内側）
            // 四辺: 外向きの法線 n と、辺に沿う向き t、辺の半分の長さ
            struct Side { Vector3 n, t; float edge, half; };
            const Side sides[] = {
                { { 0, 0, 1 }, { 1, 0, 0 }, halfD, halfW },
                { { 0, 0, -1 }, { 1, 0, 0 }, halfD, halfW },
                { { 1, 0, 0 }, { 0, 0, 1 }, halfW, halfD },
                { { -1, 0, 0 }, { 0, 0, 1 }, halfW, halfD },
            };
            for (const Side& sd : sides)
                for (int ri = 0; ri < (int)std::size(rows); ++ri)
                {
                    const Row& row = rows[ri];
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
                        const float yawDeg = randf(0.0f, 360.0f);
                        const float base = lo - targetH * 0.08f;   // 少し埋める（底の縁が浮いて見えないように）
                        if (ri > 0)
                        {
                            const float radius = 0.5f * ((pm.hi.x - pm.lo.x) + (pm.hi.z - pm.lo.z)) * 0.5f * pm.unit * scale;
                            const float out = sd.edge + row.offset + radius * 0.8f + randf(-1.0f, 1.0f);
                            const Vector3 p = sd.n * out + sd.t * (s + randf(-1.0f, 1.0f));
                            spawnVisual(pm, scale, yawDeg, p.x, p.z, base, MapData::kEdgeRock);
                            ++mountainRocks;
                            continue;
                        }

                        // 一番手前の列：岩と一緒に回した包囲箱（× edgeRockShrink）の内向きの半幅から、
                        // 箱の内側の面が縁から intrude だけ内に入る所へ原点を置く
                        const float u = pm.unit * scale;
                        const auto rot = DirectX::SimpleMath::Matrix::CreateRotationY(DirectX::XMConvertToRadians(yawDeg));
                        const Vector3 ax = Vector3::TransformNormal({ 1, 0, 0 }, rot);
                        const Vector3 az = Vector3::TransformNormal({ 0, 0, 1 }, rot);
                        const float hx = (pm.hi.x - pm.lo.x) * 0.5f * u * cfg.edgeRockShrink;
                        const float hz = (pm.hi.z - pm.lo.z) * 0.5f * u * cfg.edgeRockShrink;
                        const Vector3 mid = Vector3::TransformNormal(
                            Vector3((pm.hi.x + pm.lo.x) * 0.5f * u, 0.0f, (pm.hi.z + pm.lo.z) * 0.5f * u), rot);   // 原点 → 箱の真ん中
                        const float ext = std::fabs(ax.Dot(sd.n)) * hx + std::fabs(az.Dot(sd.n)) * hz;          // 外向きの半幅
                        const float intrude = randf(cfg.edgeRockIntrudeMin, cfg.edgeRockIntrudeMax);
                        const float out = sd.edge - intrude + ext - mid.Dot(sd.n);
                        const Vector3 p = sd.n * out + sd.t * (s + randf(-1.0f, 1.0f));
                        spawnVisual(pm, scale, yawDeg, p.x, p.z, base, MapData::kEdgeRock);
                        ++mountainRocks;
                        const Vector3 c = Vector3(p.x, 0.0f, p.z) + mid;

                        // 雑魚（格子しか見ない）：箱が 0.15m 以上入り込むマスを塞ぐ（2026-10-03、通し検査 soak）。
                        // 以前は「1/4 以上掛かる」= マスの中心から ±0.5m の小点 4 つだけで見ていて、角が 0.5m 弱
                        // 入り込む岩には縁沿いの雑魚の体が 0.3m 埋まった。小点を 5x5（一番外はマスの縁から 0.15m 内）にし、
                        // 衝突の要らない浅い岩（0.15〜0.35m）も塞ぐ
                        constexpr float kBlockIntrude = 0.15f;
                        if (intrude > kBlockIntrude)
                        {
                            auto inBox = [&](const Vector3& q)
                                {
                                    const Vector3 d = q - c;
                                    return std::fabs(d.Dot(ax)) <= hx && std::fabs(d.Dot(az)) <= hz;
                                };
                            const float reach = hx + hz;
                            int gx0, gz0, gx1, gz1;
                            grid.WorldToCell({ c.x - reach, 0.0f, c.z - reach }, gx0, gz0);
                            grid.WorldToCell({ c.x + reach, 0.0f, c.z + reach }, gx1, gz1);
                            const float off[5] = { -0.85f, -0.425f, 0.0f, 0.425f, 0.85f };   // × マスの半分
                            for (int gz = (std::max)(gz0, 1); gz <= (std::min)(gz1, gd - 2); ++gz)
                                for (int gx = (std::max)(gx0, 1); gx <= (std::min)(gx1, gw - 2); ++gx)
                                {
                                    const Vector3 cc = grid.CellToWorld(gx, gz);
                                    bool hit = false;
                                    for (int sz = 0; sz < 5 && !hit; ++sz)
                                        for (int sx = 0; sx < 5 && !hit; ++sx)
                                            hit = inBox(cc + Vector3(off[sx], 0.0f, off[sz]) * (kCs * 0.5f));
                                    if (!hit) continue;
                                    if (grid.IsWalkable(gx, gz)) ++edgeRockCells;
                                    emit.Block(&grid, gx, gz, 1, 1);
                                }
                        }

                        // 入り込みがプレイヤーの半径（0.4m）未満の岩は、外周の崖の箱がプレイヤーを先に止めるので衝突は要らない
                        // （衝突の数を減らす。静的な衝突も物理・ブロードフェーズの走査で 1 個ずつ時間を食う）
                        constexpr float kNeedCollider = 0.35f;
                        if (intrude <= kNeedCollider) continue;

                        // 衝突：回した箱（底 〜 岩の頂）。縁より内の岩だけ
                        const float y0 = base, y1 = base + targetH;
                        const Vector3 dx = ax * hx, dz = az * hz;
                        Vector3 v[8] = {
                            c - dx - dz, c + dx - dz, c + dx + dz, c - dx + dz,
                            c - dx - dz, c + dx - dz, c + dx + dz, c - dx + dz,
                        };
                        for (int k = 0; k < 4; ++k) { v[k].y = y0; v[k + 4].y = y1; }
                        // Layer_Prop（木と同じ）：プレイヤーは当たる、カメラの遮蔽の射線は見ない（毎フレーム 5 本 × 全部を回すと
                        // Debug で 0.15 ms。カメラは外周の崖の箱で止まる）
                        emit.Hull(v, Layer_Prop);
                        ++edgeRockColliders;
                    }
                }
        }

        // ---------- 遺跡の壁（Biome::Dungeon の外周）----------
        // Modular Ruins の 2m 幅の壁を辺に沿って並べ、壁の高さ（cfg.wallHeight）まで積む。一番上の段は崩れた縁。
        // 内側の面がフィールドの縁（崖のマスの内側）に来る。柱を 8m 毎、松明を 12m 毎に内側の面へ
        // （松明の位置は outTorches へ。点光源はシーンが近い物にだけ付ける）。
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
                            spawnVisual(pm, 1.0f, sd.yaw, p.x, p.z, lo + r * ph, MapData::kRuinWall);
                            ++ruinPieces;
                        }
                    }
                    if (!columns.empty())
                    {
                        const PropModel& cm = columns[0];
                        const float cw = (cm.hi.x - cm.lo.x) * cm.unit;
                        const float cd = (cm.hi.z - cm.lo.z) * cm.unit;
                        const float ch = (cm.hi.y - cm.lo.y) * cm.unit;
                        // 柱の内側の面が縁から奥行きの ruinColumnIntrude だけ内に出る（2026-10-03 までは 0.85 出ていて
                        // 衝突も無く、入り込めた）。出た分は箱で止める（向きは 90 度刻みなので AABB）
                        const float intrude = std::clamp(cfg.ruinColumnIntrude, 0.0f, 1.0f) * cd;
                        const bool alongX = std::fabs(sd.n.z) > 0.5f;   // 縁が x に沿う = 柱の幅が x
                        for (float s = -sd.half + 4.0f; s <= sd.half - 2.0f; s += 8.0f)
                        {
                            float lo, hi;
                            edgeSpan(sd.n, sd.t, sd.edge, s, 0.0f, lo, hi);
                            const Vector3 p = sd.n * (sd.edge - intrude + cd * 0.5f) + sd.t * s;
                            spawnVisual(cm, 1.0f, sd.yaw, p.x, p.z, lo, MapData::kRuinWall);
                            if (intrude > 0.0f)
                            {
                                const Vector3 cc(p.x, lo + ch * 0.5f, p.z);
                                const Vector3 chalf = alongX ? Vector3(cw * 0.5f, ch * 0.5f, cd * 0.5f)
                                                             : Vector3(cd * 0.5f, ch * 0.5f, cw * 0.5f);
                                emit.Box(cc - chalf, cc + chalf, Layer_Prop);   // 外周の岩と同じ（カメラの射線は見ない）
                            }
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
                            spawnVisual(tm, 1.0f, sd.yaw, p.x, p.z, lo + 2.0f, MapData::kTorch);
                                torchList.push_back(Vector3(p.x, lo + 2.7f, p.z) - sd.n * 0.4f);
                            ++ruinPieces;
                        }
                    }
                }
            }
        }

        // ---------- 洞の上の岩（低い山に見せる）と中の松明 ----------
        int caveRocks = 0, caveTorches = 0;
        // 岩のモデルの表・松明の条件は記録にも残す（区域を塗り替えた時に同じ物で作り直す。MapTerrainEdit::RegenZones）
        std::vector<std::string> roofRockPaths;
        const bool rimRock = cfg.rockMountains && !ruinWalls && set.nCliff > 0;
        const float rimSink = (std::max)(-cfg.edgeRockIntrudeMin, 0.0f) + 0.2f;
        if (cave)
        {
            const bool ownCliff = set.nCliff > 0;   // 遺跡は岩山の表が無いので森の岩を使う
            const char* const* paths = ownCliff ? set.cliff : F::kCliffRocks;
            const size_t count = ownCliff ? set.nCliff : std::size(F::kCliffRocks);
            for (size_t i = 0; i < count; ++i) roofRockPaths.push_back(paths[i]);
            // 岩の塊（坑 + 岩の壁）の上に 2x2 マス毎に 1 個、真ん中ほど高く。松明は坑の底の壁際に caveTorchSpacing マス毎
            caveRocks = EmitRoofRocks(emit, grid, zone, caveRing, roofTopY, cfg.roofRockMin, cfg.roofRockMax, roofRockPaths, rng);
            caveTorches = EmitCaveTorches(emit, grid,
                [&](int x, int z) { return at(x, z) == kPit && grid.IsWalkable(x, z); },   // 歩ける底（坂・降り口・岩は除く）
                caveRing, mineD, cfg.caveTorchSpacing, rimRock, rimSink, Ru::kTorch, torchList);
        }

        // ---------- 草を生やすマス ----------
        // 土の坂道・登れない台地（高さ場が 0 のまま = 箱の中に生えてしまう）・洞窟・洞の岩の壁以外。
        // 外周の崖のマスにも生やす（2026-10-03。外周の岩を外へ下げて地面が見えるようになった。高さ場は内側の隣と同じ）。
        // 崖の面そのものは GrassRenderer が高さ場の傾きで弾く
        std::vector<uint8_t> grassMask;
        {
            auto& mask = grassMask;
            mask.assign((size_t)gw * gd, 1);
            for (int z = 0; z < gd; ++z)
                for (int x = 0; x < gw; ++x)
                    if (at(x, z) == kRamp || at(x, z) == kMass || zoneAt(x, z) == kZoneMine)
                        mask[(size_t)z * gw + x] = 0;
            for (const auto& p : plateaus)
                if (!p.reachable)
                    for (int z = p.r.z; z < p.r.z + p.r.d; ++z)
                        for (int x = p.r.x; x < p.r.x + p.r.w; ++x)
                            mask[(size_t)z * gw + x] = 0;
        }

        // ---------- 三層の結果（箱・Boss の門の置き場所）----------
        Layout layout;
        if (layers)
        {
            layout.summitRamps = summitRampInfo;
            layout.mineRamps = mineRampInfo;
            // 山頂の上面の歩けるマス、洞窟の底の歩けるマス（坂は除く）、一番奥（TerrainBuild。区域を塗り替えた時も同じ関数）
            ComputeLayoutCells(grid,
                [&](int x, int z) { return zone[(size_t)z * gw + x] == kZoneSummit && at(x, z) != kSlope && grid.IsWalkable(x, z); },
                [&](int x, int z) { return zone[(size_t)z * gw + x] == kZoneMine && at(x, z) != kRamp && grid.IsWalkable(x, z); },
                mineLandings, mineD, layout.summitCells, layout.mineCells, layout.hasMineDeep, layout.mineDeep);
        }

        // ---------- 記録（MapData）の土台と見出し ----------
        // 実体・塞いだマスは Emitter が積んである。格子はここまでの結果（シーンの後処理の前）
        if (outMap)
        {
            MapData::Map& m = *outMap;
            m.seed = cfg.seed; m.biome = (int)cfg.biome; m.gw = gw; m.gd = gd;
            m.relief = cfg.relief; m.reliefSubdiv = cfg.reliefSubdiv; m.cave = cave;
            m.summitH = summitH; m.mineD = mineD; m.roofTopY = roofTopY;
            m.zone = zone; m.caveRing = caveRing;
            m.reliefPlain = relief->plain; m.reliefSummit = relief->summit;
            // 部品から作り直す用：台座で均す前の起伏、台座、仕上げの設定（箱・坂の部品は addBlock / addRamp が積んである）
            m.rawPlain = rawPlain; m.rawSummit = rawSummit;
            m.pads = pads;
            m.padMargin = cfg.padMargin; m.reliefMaxSlopeDeg = cfg.reliefMaxSlopeDeg; m.rampSlopeDeg = cfg.rampSlopeDeg;
            // 区域を塗り替えた時の作り直し用（MapTerrainEdit::RegenZones）
            m.hasZoneParams = true;
            m.floorPlainTop = floorPlainTop; m.floorSummitTop = floorSummitTop;
            m.roofBottomY = roofBottomY; m.roofCollTop = (std::max)(cfg.roofCollisionTop, roofTopY);
            m.roofRockMin = cfg.roofRockMin; m.roofRockMax = cfg.roofRockMax; m.caveTorchSpacing = cfg.caveTorchSpacing;
            m.rimRock = rimRock; m.rimSink = rimSink;
            m.roofRockModels = roofRockPaths; m.torchModel = Ru::kTorch;
            m.hasReliefParams = cfg.relief; m.reliefParams = reliefParams; m.hills = hills;
            m.walkable = grid.Walkable(); m.heights = grid.Heights();
            m.grassMask = grassMask;
            m.torches = torchList;
            m.summitCells = layout.summitCells; m.mineCells = layout.mineCells;
            m.hasMineDeep = layout.hasMineDeep; m.mineDeep = layout.mineDeep;
            for (const auto& r : layout.summitRamps) m.summitRamps.push_back({ r.top, r.down });
            for (const auto& r : layout.mineRamps) m.mineRamps.push_back({ r.top, r.down });
        }
        if (outGrassMask) *outGrassMask = std::move(grassMask);
        if (outTorches) outTorches->insert(outTorches->end(), torchList.begin(), torchList.end());
        if (outLayout) *outLayout = std::move(layout);

        // 起伏の目安（見えている平原の地面 = 歩けて台地・坂の下でないノード）: 高さの幅と傾きの分布（度）
        if (cfg.relief)
        {
            std::vector<float> slopes;
            float lo = 1e9f, hi = -1e9f;
            const int nx = relief->nx;
            for (int iz = 1; iz + 1 < relief->nz; iz += 2)
                for (int ix = 1; ix + 1 < nx; ix += 2)
                {
                    const int cx = (std::min)(ix / hsub, gw - 1), cz = (std::min)(iz / hsub, gd - 1);
                    if (relief->ZoneAtCell(cx, cz) != kZonePlain || raised[(size_t)cz * gw + cx] || !grid.IsWalkable(cx, cz)) continue;
                    const float* p = &relief->plain[(size_t)iz * nx + ix];
                    lo = (std::min)(lo, p[0]);
                    hi = (std::max)(hi, p[0]);
                    const float dx = (p[1] - p[-1]) / (2.0f * relief->step), dz = (p[nx] - p[-nx]) / (2.0f * relief->step);
                    slopes.push_back(DirectX::XMConvertToDegrees(std::atan(std::sqrt(dx * dx + dz * dz))));
                }
            if (!slopes.empty())
            {
                std::sort(slopes.begin(), slopes.end());
                auto pct = [&](float q) { return slopes[(size_t)(q * (slopes.size() - 1))]; };
                std::cout << "[Terrain] relief: plain " << lo << " .. " << hi << " m, slope p50 " << pct(0.5f)
                    << " p90 " << pct(0.9f) << " p99 " << pct(0.99f) << " max " << slopes.back() << " deg" << std::endl;
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
            << mountainRocks << " mountain rocks (" << edgeRockColliders << " edge colliders, "
            << edgeRockCells << " cells blocked), " << ruinPieces << " ruin pieces, biome " << (int)cfg.biome << ", grid "
            << gw << "x" << gd << std::endl;

        // 木・岩のモデルの大きさ（拡縮 1 倍、m）。「小さい物は見た目だけ」のしきい値を決める目安
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
