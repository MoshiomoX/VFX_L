// ============================================================
// TerrainGenerator.h
// 戦闘の地形を seed から生成する（格子に沿った「台地と坂道」の野原）。
//
// 生成するもの:
//   床（草地。色むらのある細分化メッシュ）+ 外周の崖
//   台地（平らな上面の箱。高さ数 m）… 1 段目は野原に、一部は上に 2 段目が載る
//   坂道（台地の横から降りる楔。上面は土の道）… 台地に登れるのはここだけ
//   丘（四方がなだらかな台形。どこからでも登れる）
//   自然物（KayKit Forest の木・岩・茂み・草。木は林に固まる）
//
// 格子との約束（雑魚は GPU で格子と高さ場しか見ない）:
//   台地・坂道・丘の足跡は walkable のまま、高さ場に上面の高さを書く。
//   崖（台地の側面）は「急すぎる段差」として GPU の SwarmSlopeOk と
//   FlowField::maxStep が止める。坂道は 30° 以下なので通れる。
//   坂道が 1 本も付けられなかった台地は登れないので BlockArea（上に湧かない）。
//   外周の崖は BlockArea。
//
// ※乱数は seed 固定の mt19937 で、同じ seed なら同じ地形になる
//   （「あの配置でだけ敵が引っかかる」を追える状態を保つため）
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

        // ---- 1 段目の台地 ----
        int   plateauCount = 14;       // 置こうとする数（場所が無ければ減る）
        int   plateauMin = 5;          // 一辺（マス）
        int   plateauMax = 12;
        float heightMin = 2.5f;        // 高さ m（跳躍では届かない）
        float heightMax = 4.0f;
        int   gap = 3;                 // 台地同士の最小間隔（マス）= 地上の通路の幅

        // ---- 2 段目（1 段目の上に載る小さい台地）----
        float tier2Chance = 0.45f;     // 大きい台地（一辺 8 マス以上）に載せる確率
        float tier2HeightMin = 2.0f;
        float tier2HeightMax = 3.0f;

        // ---- 坂道 ----
        int   rampWidth = 2;           // マス（4m）
        float rampSlopeDeg = 28.0f;    // 30° 以下（雑魚は 40° まで、流場は 1 マス 1.5m まで通す）

        // ---- 丘（四方が坂の低い台形）----
        int   moundCount = 10;
        float moundHeightMin = 0.8f;
        float moundHeightMax = 1.8f;
        float moundSlopeDeg = 25.0f;

        // ---- 自然物（KayKit Forest）----
        // 木と岩は置物（1 マス以上を塞ぐ。周り 1 マスは他の置物を置かないので、
        // 並んで壁になることはない）。茂みと草は見た目だけ（衝突も格子も無し）
        int treeCount = 110;      // 林（ノイズで固まる）が主、所々に 1 本
        int rockCount = 40;
        int bushCount = 160;
        int grassCount = 450;

        // ---- 外周の崖 ----
        float wallHeight = 7.0f;

        // 玩家の初期地点（場地中央）の周りは平らに空ける（マス数の半径）
        int spawnClearRadius = 8;
    };

    // 床・外周・台地・坂道・丘を生成し、grid に占用と高さを登記する。
    // 生成した Entity は outTerrain に積む（シーンが破棄用に持つ）
    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain);
}
