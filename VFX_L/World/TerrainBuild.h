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
#include <memory>
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

        // ---- CollisionMath::HeightFieldShape ----
        bool Contains(float x, float z) const override
        {
            return x >= ox && z >= oz && x <= ox + gw * kCs && z <= oz + gd * kCs;
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

        // 新しい 1 個（木 1 本・台地 1 つ…）の始まり。以後の記録は同じ group になる
        MapData::Tag Begin(MapData::Kind kind);
        // 前に振った札へ戻す（台地の坂道と本体のように、離れた所で同じ 1 個へ足す時）・記録の再生
        void SetTag(const MapData::Tag& tag) { m_Tag = tag; }

        void Box(const Vector3& lo, const Vector3& hi, uint32_t layer);          // 衝突だけの箱
        void Hull(const Vector3 world[8], uint32_t layer);                       // 衝突だけの凸体
        // 見た目の六面体（上面と側面の 2 色）。地形全部で 1 つのモデルにする（FinishVisuals）
        void Visual(const Vector3 world[8], const Vector4& top, const Vector4& side,
            int topLayer = -1, int sideLayer = -1);
        // 置物の見た目（StaticPropRenderer がまとめて描く）。model が null なら記録だけ（素材が無い PC）
        void Prop(const std::string& path, const std::shared_ptr<Model>& model,
            const Vector3& pos, float yawDeg, float scale);
        void Block(GridWorld& grid, int x, int z, int w, int d);                 // 格子を塞ぐ

        // 積んだ見た目を 1 つのモデルの実体にする（最後に 1 回）
        void FinishVisuals(ID3D11Device* device, TerrainGenerator::Biome biome, const float* refLum);

        MapData::Map* Rec() const { return m_Rec; }

    private:
        Registry& m_Reg;
        std::vector<Entity>& m_Out;
        MapData::Map* m_Rec = nullptr;
        MapData::Tag m_Tag;
        uint32_t m_NextGroup = 1;   // rec が無い時用
        PrimitiveBuilder::HexahedronBatch m_Batch;
    };

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
    };
    Entity SpawnGround(Registry& reg, ID3D11Device* device, const GridWorld& grid,
        const std::shared_ptr<ReliefField>& relief, const std::vector<uint8_t>& caveRing,
        const GroundParams& gp, const float* refLum);
    Entity SpawnHeightField(Registry& reg, const GridWorld& grid,
        const std::shared_ptr<ReliefField>& relief, float summitH);
}
