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
#include <SimpleMath.h>
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

        // 障害物のうち台形柱（斜面つき、衝突は Convex）にする割合と斜面の角度。
        // 接地判定は normal.y > 0.5（60°）なので、それ未満なら歩いて登れる
        float trapezoidRatio = 0.4f;
        float slopeDeg = 40.0f;
    };

    // 床・壁・障害物を生成し、grid に占用を登記する。
    // 生成した Entity は outTerrain に積む（シーンが破棄用に持つ）
    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain);

    // 台形柱を 1 つ置く（手で配置する用。Generate も内部でこれを使う）。
    //   gx, gz, w, d : 格子のマス（左下と大きさ）
    //   height       : 上面が残らない場合は低くなる
    //   slopeDeg     : 斜面の角度（60° 未満なら歩いて登れる）
    //   alongX       : true = X 方向の両側を斜面に、false = Z 方向
    Entity SpawnTrapezoid(Registry& reg, ID3D11Device* device, GridWorld& grid,
        int gx, int gz, int w, int d, float height, float slopeDeg, bool alongX,
        const DirectX::SimpleMath::Vector4& color);
}