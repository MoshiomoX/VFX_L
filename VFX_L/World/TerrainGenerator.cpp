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
#include <random>
#include <cmath>
#include <iostream>

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

namespace
{
    // 格子对齐の静的 Box を1つ置き、grid に登記する
    Entity SpawnGridBox(Registry& reg, ID3D11Device* device, GridWorld& grid,
        int gx, int gz, int w, int d, float height, const Vector4& color)
    {
        const float cs = GridWorld::kCellSize;
        Vector3 half = { w * cs * 0.5f, height * 0.5f, d * cs * 0.5f };
        Vector3 pos = { grid.OriginX() + gx * cs + half.x,
                        half.y,
                        grid.OriginZ() + gz * cs + half.z };
        Entity e = TestSpawner::SpawnStaticBox(reg, pos, half);

        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateBox(device, half, color);
        reg.Add<ModelComponent>(e, mc);

        grid.BlockArea(gx, gz, w, d);
        return e;
    }
}

namespace TerrainGenerator
{
    // 格子対齐の台形柱（斜面つき障害物）。
    // 底面は w×d マス、上面を alongX の軸方向の両側から inset だけ狭める
    // （断面が台形。inset = h / tan(slope) なので登れる角度は slopeDeg で決まる）。
    // 衝突は Convex（6 平面）、格子には箱と同じ足跡で登記する（雑魚は壁扱い）
    Entity SpawnTrapezoid(Registry& reg, ID3D11Device* device, GridWorld& grid,
        int gx, int gz, int w, int d, float height, float slopeDeg, bool alongX,
        const Vector4& color)
    {
        using CollisionMath::Convex;
        const float cs = GridWorld::kCellSize;
        const Vector3 half = { w * cs * 0.5f, height * 0.5f, d * cs * 0.5f };
        const Vector3 pos = { grid.OriginX() + gx * cs + half.x,
                              half.y,
                              grid.OriginZ() + gz * cs + half.z };

        // 上面をどれだけ狭めるか。上面が消えない（最低 0.5m 残す）ように高さを削る
        const float tanS = std::tan(DirectX::XMConvertToRadians(slopeDeg));
        const float bottomW = alongX ? half.x * 2.0f : half.z * 2.0f;
        float h = height;
        float inset = h / tanS;
        if (bottomW - 2.0f * inset < 0.5f)
        {
            inset = (bottomW - 0.5f) * 0.5f;
            h = inset * tanS;
        }
        const float hy = h * 0.5f;
        const float ix = alongX ? inset : 0.0f;
        const float iz = alongX ? 0.0f : inset;

        // ローカル頂点（中心原点。下面 0-3、上面 4-7）
        Vector3 v[8] = {
            { -half.x, -hy, -half.z }, { +half.x, -hy, -half.z },
            { +half.x, -hy, +half.z }, { -half.x, -hy, +half.z },
            { -half.x + ix, +hy, -half.z + iz }, { +half.x - ix, +hy, -half.z + iz },
            { +half.x - ix, +hy, +half.z - iz }, { -half.x + ix, +hy, +half.z - iz },
        };

        Entity e = reg.Create();

        TransformComponent tf;
        tf.position = { pos.x, hy, pos.z };
        reg.Add<TransformComponent>(e, tf);

        ColliderComponent col;
        col.shape = ColliderShape::Convex;
        col.hull = CollisionMath::ConvexFromHexahedron(v);
        col.halfExtents = { half.x, hy, half.z };   // 広相位用の包囲箱
        col.layer = Layer_Terrain;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.isStatic = true;
        rb.useGravity = false;
        reg.Add<RigidbodyComponent>(e, rb);

        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateHexahedron(device, v, color);
        reg.Add<ModelComponent>(e, mc);

        // 格子は塞がない（雑魚が登る）。代わりに足跡の高さ場へ上面の高さを書く。
        // 高さ = 凸体の「上を向いた平面」のうち一番低い物（下から見て初めて当たる面）。
        // 足跡の外側（斜面が地面に落ちた先）は 0 のまま
        {
            const int sub = GridWorld::kHeightSub;
            const auto& hull = col.hull;
            for (int hz = gz * sub; hz < (gz + d) * sub; ++hz)
                for (int hx = gx * sub; hx < (gx + w) * sub; ++hx)
                {
                    const Vector3 p = grid.HeightCellToWorld(hx, hz);
                    const float lx = p.x - tf.position.x;   // ローカル xz
                    const float lz = p.z - tf.position.z;
                    float top = 1e9f;
                    for (int i = 0; i < hull.count; ++i)
                    {
                        const auto& pl = hull.planes[i];
                        if (pl.n.y <= 0.1f) continue;   // 上向きの面だけ
                        top = (std::min)(top, (pl.d - pl.n.x * lx - pl.n.z * lz) / pl.n.y);
                    }
                    if (top < 1e8f)
                        grid.SetHeight(hx, hz, (std::max)(0.0f, top + tf.position.y));   // ローカル → ワールド
                }
        }
        return e;
    }

    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain)
    {
        const float cs = GridWorld::kCellSize;
        const float W = grid.WorldWidth();
        const float D = grid.WorldDepth();

        // ---------- 床 ----------
        // 床は格子に登記しない（上を歩くものなので通行を塞がない）。
        // 上面が y=0 に来るように半分沈める
        {
            Entity e = TestSpawner::SpawnStaticBox(reg,
                { 0.0f, -0.5f, 0.0f },
                { W * 0.5f, 0.5f, D * 0.5f });
            ModelComponent mc;
            mc.model = PrimitiveBuilder::CreateBox(device,
                { W * 0.5f, 0.5f, D * 0.5f }, { 0.45f, 0.45f, 0.50f, 1 });
            reg.Add<ModelComponent>(e, mc);
            outTerrain.push_back(e);
        }

        // ---------- 外周の壁 ----------
        // 1マス幅で四辺を囲む。場外へ出る・落ちるをここで殺す。
        // 壁も格子に登記される（A* が外周を通れない図になる）
        const int gw = grid.Width();
        const int gd = grid.Depth();
        const float wallH = 3.0f;
        const Vector4 wallCol = { 0.35f, 0.35f, 0.40f, 1 };

        outTerrain.push_back(SpawnGridBox(reg, device, grid, 0, 0, gw, 1, wallH, wallCol));          // 手前
        outTerrain.push_back(SpawnGridBox(reg, device, grid, 0, gd - 1, gw, 1, wallH, wallCol));     // 奥
        outTerrain.push_back(SpawnGridBox(reg, device, grid, 0, 1, 1, gd - 2, wallH, wallCol));      // 左
        outTerrain.push_back(SpawnGridBox(reg, device, grid, gw - 1, 1, 1, gd - 2, wallH, wallCol)); // 右

        // ---------- ランダム障害物 ----------
        // seed 固定の mt19937。rand() を使わないのは再現性のため
        std::mt19937 rng(cfg.seed);
        std::uniform_int_distribution<int> distSize(cfg.minSize, cfg.maxSize);
        std::uniform_real_distribution<float> distH(cfg.minHeight, cfg.maxHeight);
        std::uniform_real_distribution<float> distTrap(0.0f, 1.0f);

        // 玩家初期地点（場地中央）の周りは空ける
        const int cx = gw / 2;
        const int cz = gd / 2;

        // 配置済みの足跡。台形柱は格子を塞がない（walkable のまま）ので、
        // 重ね置きの判定は walkable とは別にここで持つ
        std::vector<uint8_t> occupied((size_t)gw * gd, 0);
        auto areaFree = [&](int x0, int z0, int w0, int d0)
            {
                for (int z = z0; z < z0 + d0; ++z)
                    for (int x = x0; x < x0 + w0; ++x)
                        if (occupied[(size_t)z * gw + x]) return false;
                return true;
            };
        auto markArea = [&](int x0, int z0, int w0, int d0)
            {
                for (int z = z0; z < z0 + d0; ++z)
                    for (int x = x0; x < x0 + w0; ++x)
                        occupied[(size_t)z * gw + x] = 1;
            };

        int placed = 0;
        int attempts = 0;
        const int maxAttempts = cfg.obstacleCount * 10;

        while (placed < cfg.obstacleCount && attempts < maxAttempts)
        {
            ++attempts;

            const int w = distSize(rng);
            const int d = distSize(rng);

            std::uniform_int_distribution<int> distX(1, gw - 1 - w);
            std::uniform_int_distribution<int> distZ(1, gd - 1 - d);
            const int gx = distX(rng);
            const int gz = distZ(rng);

            // 初期地点の近くは置かない
            if (std::abs(gx + w / 2 - cx) <= cfg.spawnClearRadius &&
                std::abs(gz + d / 2 - cz) <= cfg.spawnClearRadius)
                continue;

            // 既存と重ねない。
            // ※間隔は空けない（隣接は許す）。狭い通路や袋小路が出来るが、
            //   それこそ A* に食わせたい形なので歓迎する
            if (!grid.IsAreaWalkable(gx, gz, w, d) || !areaFree(gx, gz, w, d))
                continue;
            markArea(gx, gz, w, d);

            const float h = distH(rng);
            const Vector4 col = { 0.55f + 0.15f * (float)(placed % 3),
                                  0.45f, 0.35f, 1.0f };

            // 一部を台形柱にする（斜面の登り降りと Convex 判定の実地テスト）。
            // 1 マス幅だと上面が残らないので 2 マス以上の辺に沿って狭める
            const bool trapezoid = (distTrap(rng) < cfg.trapezoidRatio) && (w >= 2 || d >= 2);
            if (trapezoid)
            {
                const bool alongX = (w >= d);
                const Vector4 tcol = { 0.40f, 0.55f + 0.1f * (float)(placed % 2), 0.45f, 1.0f };
                outTerrain.push_back(SpawnTrapezoid(reg, device, grid, gx, gz, w, d, h,
                    cfg.slopeDeg, alongX, tcol));
            }
            else
                outTerrain.push_back(SpawnGridBox(reg, device, grid, gx, gz, w, d, h, col));
            ++placed;
        }

        std::cout << "[Terrain] generated: " << placed << " obstacles ("
            << attempts << " attempts), grid " << gw << "x" << gd << std::endl;
    }
}