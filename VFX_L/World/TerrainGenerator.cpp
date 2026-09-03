// ============================================================
// TerrainGenerator.cpp
// ============================================================
#include "World/TerrainGenerator.h"
#include "World/GridWorld.h"
#include "ECS/Registry.h"
#include "Component/ModelComponent.h"
#include "Debug/TestSpawner.h"
#include "Graphics/PrimitiveBuilder.h"
#include <random>
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

        // 玩家初期地点（場地中央）の周りは空ける
        const int cx = gw / 2;
        const int cz = gd / 2;

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
            if (!grid.IsAreaWalkable(gx, gz, w, d))
                continue;

            const float h = distH(rng);
            const Vector4 col = { 0.55f + 0.15f * (float)(placed % 3),
                                  0.45f, 0.35f, 1.0f };

            outTerrain.push_back(SpawnGridBox(reg, device, grid, gx, gz, w, d, h, col));
            ++placed;
        }

        std::cout << "[Terrain] generated: " << placed << " obstacles ("
            << attempts << " attempts), grid " << gw << "x" << gd << std::endl;
    }
}