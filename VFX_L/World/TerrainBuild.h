// ============================================================
// TerrainBuild.h
// 戦闘の地形を「建てる」側（2026-10-05、TerrainGenerator.cpp から分けた）。
//
// TerrainGenerator::Generate は seed から何をどこへ置くかを決め、建てるのはここの出口（Emitter）に頼む。
// Emitter は実体を作りながら同じ内容を MapData::Map へ記録する。
// TerrainGenerator::BuildFromMap（TerrainBuild.cpp）は記録を同じ Emitter へ流し直すだけなので、
// 「保存した地図を読んだ結果」と「生成した結果」は同じ建て方の同じ引数になる。
//
// World/ の中だけで使う（シーンからは TerrainGenerator.h）
// ============================================================
#pragma once
#include "World/TerrainGenerator.h"
#include "World/MapData.h"
#include "World/GridWorld.h"
#include "ECS/Registry.h"
#include "Component/ColliderComponent.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Renderer/TerrainSurface.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

class Model;

namespace TerrainBuild
{
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Vector4;

    inline constexpr float kCs = GridWorld::kCellSize;

    // ---- 色（明るい草地）----
    // 頂点色は線形のアルベド（CompositePS が最後に 1/2.2 のガンマを掛ける）。
    // 画面で見せたい sRGB の色を 2.2 乗した値で持つ（括弧内が sRGB）
    // 面（Biome）ごとの色の組
    struct Palette
    {
        Vector4 groundDark, groundLight, groundDry;   // 床：暗い / 明るい / 所々の別色（乾いた草・砂利・苔）
        Vector4 plateauTop, cliff, cliffHigh;         // 台地の上面 / 側面 / 2 段目の側面
        Vector4 rampTop, wallRock;                    // 坂道の上面 / 外周の箱（岩山でない時）
        Vector4 mineDark, mineLight;                  // 洞窟の底：暗い / 明るい（土と砂利）
    };
    const Palette& PaletteFor(TerrainGenerator::Biome b);

    Vector4 LerpColor(const Vector4& a, const Vector4& b, float t);
    float Hash01(int x, int z, uint32_t seed);                 // 0..1
    float ValueNoise(float x, float z, uint32_t seed);         // 0..1
    float GradNoise(float x, float z, uint32_t seed);          // およそ -1..1
    Vector4 MineColor(float x, float z, uint32_t seed, const Palette& P);   // 洞窟の底の色

    // 軸平行の箱の 8 頂点。順は ConvexFromHexahedron / CreateHexahedron と同じ:
    // 下 0-3 = (-x-z, +x-z, +x+z, -x+z)、上 4-7 がそれぞれの真上
    void BoxVerts(Vector3 v[8], const Vector3& lo, const Vector3& hi);

    // ---- 格子の矩形・高さ場の補助（生成と、部品からの作り直し MapEdit が共用）----
    // 坂が台地のどちら側に付くか（= 降りていく向き）。MapData::RampPart::side の値と同じ
    enum class Side { PosX, NegX, PosZ, NegZ };
    struct Rect { int x, z, w, d; };   // 格子のマス（左下と大きさ）

    Vector3 RectMin(const GridWorld& g, const Rect& r);                  // 格子の矩形の下隅（世界、y = 0）
    void RaiseRect(GridWorld& g, const Rect& r, float h);                // 足跡の高さ場を h まで上げる（台地の上面）
    // 凸体の上面を足跡の高さ場へ（坂道・高台の坂）
    void WriteHullHeights(GridWorld& g, const Rect& r, const CollisionMath::Convex& hull, const Vector3& center);
    // 台地 p の side 側の外に、辺に沿って offset マス目から幅 rw、長さ len
    Rect RampRect(const Rect& p, Side s, int offset, int rw, int len);
    Rect LandingRect(const Rect& ramp, Side s);                          // 坂道を降りた先の 1 マス幅の帯
    // 隣のノードとの傾きを軸方向で tanMax までに抑える（fixed のノードは動かさない）
    void LimitSlope(std::vector<float>& h, int nx, int nz, float step, float tanMax, const std::vector<uint8_t>* fixed);

    // ============================================================
    // 起伏（2026-10-04、ユーザー：純平地をやめて、緩い丘の斜面に小さい土饅頭が載った地面に）。
    // 平原と山頂の上面の起伏を 0.5m 毎のノード（フィールドの隅から隅まで、高さ場と同じ細かさ）に持ち、間は双線形。
    // 洞窟の底は平ら。区域の境（崖）を挟んでは混ぜない = 高さも法線も「その点のマスの区域の式」で出す。
    // 戦闘の地面の衝突（ColliderShape::HeightField）もこれに問い合わせる（Generate が作り、衝突の実体が持つ）
    // ============================================================
    class ReliefField : public CollisionMath::HeightFieldShape
    {
    public:
        enum : uint8_t { kPlain = 0, kSummit = 1, kMine = 2 };
        int gw = 0, gd = 0;              // マス
        float ox = 0.0f, oz = 0.0f;      // フィールドの隅（世界）
        int nx = 0, nz = 0;              // ノード
        float step = kCs / GridWorld::kHeightSub;
        float summitH = 0.0f, mineD = 0.0f;
        std::vector<uint8_t> zone;       // マス毎の区域
        std::vector<float> plain, summit;   // ノード毎の起伏（区域の基準の高さからの差）

        void Init(const GridWorld& g, const std::vector<uint8_t>& zones, float summitHeight, float mineDepth)
        {
            gw = g.Width(); gd = g.Depth();
            ox = g.OriginX(); oz = g.OriginZ();
            nx = gw * GridWorld::kHeightSub + 1;
            nz = gd * GridWorld::kHeightSub + 1;
            zone = zones;
            summitH = summitHeight;
            mineD = mineDepth;
            plain.assign((size_t)nx * nz, 0.0f);
            summit.assign((size_t)nx * nz, 0.0f);
        }
        float NodeX(int ix) const { return ox + ix * step; }
        float NodeZ(int iz) const { return oz + iz * step; }
        float Sample(const std::vector<float>& a, float x, float z) const
        {
            const float fx = std::clamp((x - ox) / step, 0.0f, (float)(nx - 1));
            const float fz = std::clamp((z - oz) / step, 0.0f, (float)(nz - 1));
            const int ix = (std::min)((int)fx, nx - 2), iz = (std::min)((int)fz, nz - 2);
            const float tx = fx - ix, tz = fz - iz;
            const float* p = &a[(size_t)iz * nx + ix];
            return (p[0] * (1.0f - tx) + p[1] * tx) * (1.0f - tz) + (p[nx] * (1.0f - tx) + p[nx + 1] * tx) * tz;
        }
        // 外周のマスは内側の隣と同じ区域
        uint8_t ZoneAtCell(int gx, int gz) const
        {
            gx = std::clamp(gx, 1, gw - 2);
            gz = std::clamp(gz, 1, gd - 2);
            return zone[(size_t)gz * gw + gx];
        }
        uint8_t ZoneAt(float x, float z) const
        {
            return ZoneAtCell((int)std::floor((x - ox) / kCs), (int)std::floor((z - oz) / kCs));
        }
        float SurfaceIn(uint8_t k, float x, float z) const
        {
            switch (k)
            {
            case kSummit: return summitH + Sample(summit, x, z);
            case kMine:   return -mineD;
            default:      return Sample(plain, x, z);
            }
        }

        // 場外にも地面がある範囲（m）。見た目の場外の地面（kSkirt 40m）と同じ：高さは縁のノードの値（Sample / ZoneAt が端へ丸める）。
        // 外周の空気壁を石の衝突に替えた時（2026-10-06）、石の裏へ出た所に衝突の地面が無くて落ちた
        float margin = 40.0f;

        // ---- CollisionMath::HeightFieldShape ----
        bool Contains(float x, float z) const override
        {
            return x >= ox - margin && z >= oz - margin && x <= ox + gw * kCs + margin && z <= oz + gd * kCs + margin;
        }
        float Height(float x, float z) const override { return SurfaceIn(ZoneAt(x, z), x, z); }
        Vector3 Normal(float x, float z) const override
        {
            const uint8_t k = ZoneAt(x, z);
            const float e = step * 0.5f;
            const float dx = (SurfaceIn(k, x + e, z) - SurfaceIn(k, x - e, z)) / (2.0f * e);
            const float dz = (SurfaceIn(k, x, z + e) - SurfaceIn(k, x, z - e)) / (2.0f * e);
            Vector3 n(-dx, 1.0f, -dz);
            n.Normalize();
            return n;
        }
    };

    // ============================================================
    // 建てる出口。実体を作り（outTerrain へ積む）、rec があれば同じ内容を記録する。
    // 記録には今の札（kind + group）が付く。Begin で新しい group を振る
    // ============================================================
    class Emitter
    {
    public:
        Emitter(Registry& reg, std::vector<Entity>& outTerrain, MapData::Map* rec);
        explicit Emitter(MapData::Map* rec);   // 記録だけ（実体を作らない。MapEdit が部品から記録を作り直す時）

        // 新しい 1 個（木 1 本・台地 1 つ…）の始まり。以後の記録は同じ group になる
        MapData::Tag Begin(MapData::Kind kind);
        // 前に振った札へ戻す（台地の坂道と本体のように、離れた所で同じ 1 個へ足す時）・記録の再生
        void SetTag(const MapData::Tag& tag) { m_Tag = tag; }

        void Box(const Vector3& lo, const Vector3& hi, uint32_t layer);          // 衝突だけの箱
        void Hull(const Vector3 world[8], uint32_t layer, bool wallOnly = false);   // 衝突だけの凸体（wallOnly = 水平にしか押さない = 登れない）
        // 見た目の六面体（上面と側面の 2 色）。地形全部で 1 つのモデルにする（FinishVisuals）
        void Visual(const Vector3 world[8], const Vector4& top, const Vector4& side,
            int topLayer = -1, int sideLayer = -1);
        // 置物の見た目（StaticPropRenderer がまとめて描く）。model が null なら記録だけ（素材が無い PC）
        // stretch = 軸ごとの倍率（モデルのローカル軸。巨石を辺に沿って引き伸ばす用）。最終の倍率 = scale × stretch。
        // collide = モデルの凸包（Model::GetHullPoints）で衝突を付ける（Layer_Terrain。巨石。2026-10-06）。
        // 衝突の実体は記録の Prop::collide から建て直せるので、記録は置物 1 つだけ
        void Prop(const std::string& path, const std::shared_ptr<Model>& model,
            const Vector3& pos, float yawDeg, float scale, const Vector3& stretch = Vector3::One, bool collide = false);
        void SetPropColliders(bool on) { m_PropColliders = on; }   // false = collide の置物でも衝突を建てない（エディタの表示）
        void Block(GridWorld* grid, int x, int z, int w, int d);                 // 格子を塞ぐ（grid が無ければ記録だけ）

        // 積んだ見た目を 1 つのモデルの実体にする（最後に 1 回）
        void FinishVisuals(ID3D11Device* device, TerrainGenerator::Biome biome, const float* refLum);

        MapData::Map* Rec() const { return m_Rec; }
        const MapData::Tag& Tag() const { return m_Tag; }

    private:
        Registry* m_Reg = nullptr;                 // 無ければ記録だけ
        std::vector<Entity>* m_Out = nullptr;
        MapData::Map* m_Rec = nullptr;
        MapData::Tag m_Tag;
        uint32_t m_NextGroup = 1;   // rec が無い時用
        bool m_PropColliders = true;
        PrimitiveBuilder::HexahedronBatch m_Batch;
    };

    // ---- 置物の凸包（巨石の衝突。2026-10-06）----
    // モデルの凸包の点を世界へ（pos / yaw / scale × stretch。Emitter::Prop と同じ置き方）
    std::vector<Vector3> PropHullWorld(const Model& model, const Vector3& pos, float yawDeg, float scale, const Vector3& stretch);
    // 世界の点の凸包の衝突（Entity の位置 = 包囲箱の真ん中）
    Entity SpawnPointHullCollider(Registry& reg, const std::vector<Vector3>& world, uint32_t layer);
    // 凸包の足元に掛かるマスを塞ぐ（マスの 5x5 の小点のどれかが、その点の地面 + 0.6m で凸包の中）。塞いだ数を返す
    int BlockCellsUnderHull(Emitter& emit, GridWorld& grid, const std::vector<Vector3>& world);

    // ---- 起伏の合成：素の起伏 + 台座（生成と MapEdit が同じ関数を通る = 同じ結果）----
    // 洞窟の周りを平原の 0 に均す重み（ノード毎。洞窟が無ければ全部 0）
    void ComputeMineWeights(const std::vector<uint8_t>& zone, int gw, int gd, int padMargin, std::vector<float>& out);
    // ノードの範囲（両端含む）の面を、素の起伏 raw と zone の台座（+ mineW）から作り直す
    void ComposeRelief(std::vector<float>& arr, const std::vector<float>& raw, const std::vector<MapData::Pad>& pads,
        uint8_t zone, const std::vector<float>* mineW, int nx, int nz, int ix0, int iz0, int ix1, int iz1);
    // 仕上げ：急な所を抑える（台座の芯は動かさない）
    void LimitReliefSlopes(std::vector<float>& arr, const std::vector<MapData::Pad>& pads, uint8_t zone,
        const std::vector<float>* mineW, int nx, int nz, float step, float maxSlopeDeg);

    // 外周の縁を碗のように持ち上げる（2026-10-06、Megabonk 風：地面そのものが縁で急な崖になる。空気壁も石の衝突も要らない）。
    // 傾きの制限の後に掛ける（制限に均されないよう）。縁から width m 内側で height m まで、t^1.6 の曲線（上ほど急、65〜75°）。
    // 生成と MapTerrainEdit::Rederive が同じ関数を通る
    void ApplyRimRise(std::vector<float>& arr, int nx, int nz, float step, float width, float height);

    // ---- 素の起伏（台座で均す前）= ノイズの丘 + 丘の部品 → 傾きを抑える ----
    void BuildReliefNoise(const MapData::ReliefParams& rp, uint32_t seed, int gw, int gd,
        std::vector<float>& plain, std::vector<float>& summit);
    void AddHillsAndLimit(const std::vector<MapData::Hill>& hills, float summitMul, float maxSlopeDeg, int gw, int gd,
        std::vector<float>& plain, std::vector<float>& summit);

    // ---- 地形の部品を建てる。grid があれば高さ場・通行も書く。origin = 格子の原点を知るための格子 ----
    void EmitBlockPart(Emitter& emit, GridWorld* grid, const GridWorld& origin, const MapData::BlockPart& p);
    void RampVerts(const GridWorld& origin, const MapData::RampPart& p, Vector3 v[8]);
    void EmitRampPart(Emitter& emit, GridWorld* grid, const GridWorld& origin, const MapData::RampPart& p);

    // ---- 巨石（Rock-Set を数個引き伸ばして山に見せる。TerrainBoulders.cpp、2026-10-06）----
    struct RimBoulderParams
    {
        int   perSide = 4;                      // 一辺の数（1 列。中心が縁の線の上）
        float frontHMin = 28.0f, frontHMax = 40.0f;   // 辺の石の高さ（m、heightMul を掛ける前）
        float backHMin = 34.0f, backHMax = 46.0f;     // 四隅
        float depth = 30.0f;                    // 石の厚み（縁をまたぐ向き m。半分が場内）
        float sink = 0.22f;                     // 高さに対する埋める割合
        float heightMul = 1.0f;                 // Config::mountainScale
        float centerOut = 0.0f;                 // 箱の真ん中を縁の線から外へずらす量（m。負 = 内へ）
        float intrude = -0.6f;                  // （衝突なしの旧配置用）箱の内側の面：縁から内に入る量（負 = 外）
        float yawJitterDeg = 12.0f;
        bool  collide = false;                  // 石の凸包で衝突（Prop::collide）+ 足元のマスを塞ぐ（grid があれば）
        // 石の後ろの岩色の壁（見た目だけ）：石と石の丸い端の間から空が見えないように。高さは縁の地面から
        float backdropHeight = 14.0f;           // 0 = 無し
        float backdropOut = 0.0f;               // 縁から外へ（0 = 縁の線の上。石の間の隙間の突き当たりがこの壁になる）
        Vector4 backdropColor = { 0.3f, 0.3f, 0.32f, 1.0f };
    };
    // 辺の地面の高さの範囲（n = 外向き、t = 辺に沿う向き、edge = 縁の座標、s = 辺に沿う位置、halfLen = 調べる半幅）
    using EdgeSpanFn = std::function<void(const Vector3& n, const Vector3& t, float edge, float s, float halfLen, float& lo, float& hi)>;
    // grid = 衝突を付ける時に足元のマスを塞ぐ先（null = 塞がない）。outBlocked に塞いだマスの数
    int EmitRimBoulders(Emitter& emit, GridWorld* grid, float halfW, float halfD, const RimBoulderParams& P,
        const std::vector<std::string>& modelPaths, const EdgeSpanFn& edgeSpan, std::mt19937& rng, int* outBlocked = nullptr);
    // 洞窟の上の山：塊の包囲矩形に真ん中 1 個 + 両端 1 個ずつ（rockMax / rockMin は EmitRoofRocks と同じ意味の高さ）
    int EmitRoofBoulders(Emitter& emit, const GridWorld& origin, const std::vector<uint8_t>& zone,
        const std::vector<uint8_t>& ring, float roofTopY, float rockMin, float rockMax,
        const std::vector<std::string>& modelPaths, std::mt19937& rng);

    // ---- 区域（平原 / 山頂 / 洞窟）の形から決まる物（TerrainZones.cpp。生成と MapTerrainEdit::RegenZones が共用）----
    // マスの印（mask == value）を軸平行の矩形の組に分ける
    std::vector<Rect> MaskRects(const std::vector<uint8_t>& mask, int w, int d, uint8_t value);
    // 床の縦の壁になる衝突の箱（平原・洞窟の底・山頂）
    void EmitFloorBoxes(Emitter& emit, const GridWorld& origin, const std::vector<uint8_t>& zone,
        float floorBottom, float plainTop, float summitTop, float mineD);
    // 洞の岩の壁（坑の 8 近傍、口を除く）と口の印
    void ComputeCaveRing(const std::vector<uint8_t>& zone, const std::vector<Rect>& mouths, int gw, int gd,
        std::vector<uint8_t>& ring, std::vector<uint8_t>& mouthMask);
    // 洞窟の屋根：岩の壁のマスを塞ぐ、壁・屋根・梁の衝突、屋根・梁の見た目
    void EmitCaveRoof(Emitter& emit, GridWorld* grid, const GridWorld& origin, const std::vector<uint8_t>& zone,
        const std::vector<uint8_t>& ring, const std::vector<uint8_t>& mouthMask,
        float roofBottomY, float roofTopY, float collTop, const Vector4& cliffHigh);
    // 洞の上に積む岩。置いた数を返す
    int EmitRoofRocks(Emitter& emit, const GridWorld& origin, const std::vector<uint8_t>& zone,
        const std::vector<uint8_t>& ring, float roofTopY, float rockMin, float rockMax,
        const std::vector<std::string>& modelPaths, std::mt19937& rng);
    // 洞の中の松明（pitFloor = 歩ける坑の底のマスか）。灯りの位置を outLights へ足す。置いた数を返す
    int EmitCaveTorches(Emitter& emit, const GridWorld& origin, const std::function<bool(int, int)>& pitFloor,
        const std::vector<uint8_t>& ring, float mineD, int spacing, bool rimRock, float rimSink,
        const std::string& torchPath, std::vector<Vector3>& outLights);
    // 三層の結果：山頂・洞窟の歩けるマスと、洞窟の一番奥
    void ComputeLayoutCells(const GridWorld& origin, const std::function<bool(int, int)>& summitCell,
        const std::function<bool(int, int)>& mineFloor, const std::vector<Rect>& mineLandings, float mineD,
        std::vector<int>& summitCells, std::vector<int>& mineCells, bool& hasMineDeep, Vector3& mineDeep);

    // テクスチャの層毎の「元の頂点色の基準の明るさ」（TerrainSurface。頂点色の明るさ / これ をテクスチャに薄く掛けて色むらを残す）。
    // 地面・洞の底は色の関数をフィールドに 48x48 点で平均、他はその層の配色
    void ComputeRefLum(uint32_t seed, TerrainGenerator::Biome biome, int gridW, float out[TerrainSurface::LayerCount]);

    // 床の見た目（段々のメッシュ + 起伏）と、歩く面の衝突（高さ場）
    struct GroundParams
    {
        uint32_t seed = 0;
        TerrainGenerator::Biome biome = TerrainGenerator::Biome::Grassland;
        bool  relief = false;
        int   reliefSubdiv = 1;
        bool  cave = false;
        float roofTopY = 0.0f;
        float summitH = 0.0f, mineD = 0.0f;
        float rimWidth = 0.0f;   // 縁の碗の斜面の幅（m。0 = 無し）：その帯は崖の層で描く
    };
    Entity SpawnGround(Registry& reg, ID3D11Device* device, const GridWorld& grid,
        const std::shared_ptr<ReliefField>& relief, const std::vector<uint8_t>& caveRing,
        const GroundParams& gp, const float* refLum);
    Entity SpawnHeightField(Registry& reg, const GridWorld& grid,
        const std::shared_ptr<ReliefField>& relief, float summitH);
}
