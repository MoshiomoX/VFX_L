// ============================================================
// TerrainGenerator.h
// 格子に沿ったテスト地形の生成。
//
// 生成するもの:
//   床 1 枚 + 外周の壁 + 格子対齐のランダム障害物
// 障害物は GridWorld に登記される（= A* の図がここで出来上がる）。
//
// ※乱数は seed 固定で再現できるようにする。
//   「あの配置でだけ敵が引っかかる」を追える状態を保つため
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include <vector>
#include <cstdint>

struct ID3D11Device;
class Registry;
class GridWorld;

namespace TerrainGenerator
{
    struct Config
    {
        uint32_t seed = 1;
        int obstacleCount = 40;      // 置く障害物の数（置けなければ減る）
        int minSize = 1;             // 障害物の一辺（マス）
        int maxSize = 3;
        float minHeight = 1.0f;      // 障害物の高さ（m。見た目と衝突のみ、格子には無関係）
        float maxHeight = 3.0f;

        // 玩家の初期地点の周りは空けておく（マス数の半径）
        int spawnClearRadius = 3;
    };

    // 床・壁・障害物を生成し、grid に占用を登記する。
    // 生成した Entity は outTerrain に積む（シーンが破棄用に持つ）
    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain);
}