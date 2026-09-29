// ============================================================
// TerrainGenerator.h
// 戦闘の地形を seed から生成する（格子に沿った「台地と坂道」の野原）。
//
// 生成するもの:
//   床（草地。色むらのある細分化メッシュ）+ 外周の崖
//   台地（平らな上面の箱。高さ数 m）… 1 段目は野原に、一部は上に 2 段目が載る
//   坂道（台地の横から降りる楔。上面は土の道）… 台地に登れるのはここだけ
//   高台（高さ 5〜7m。一面だけが 16〜20° の長い坂、残り三面は崖。滑り込みで加速する場所）
//        … 一部は奥に同じ向きの坂を持つ 2 段目が載り、地面・1 段目・2 段目の 3 段になる
//   自然物（KayKit Forest の木・岩・茂み。木は林に固まる）。草は GrassRenderer が GPU で生やす
//
// 格子との約束（雑魚は GPU で格子と高さ場しか見ない）:
//   台地・坂道・高台の足跡は walkable のまま、高さ場に上面の高さを書く。
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

        // ---- 高台（一面だけが長い坂、残り三面は崖）----
        // 台地より先に置く（場所を取るので後回しにすると入らない）。坂の向きは高台ごとにランダム。
        // 坂の長さ = 高さ / tan(角度)：6m・18° で約 18m
        int   terraceCount = 4;
        float terraceHeightMin = 5.0f;
        float terraceHeightMax = 7.0f;
        float terraceSlopeMin = 16.0f;   // 度（雑魚の 40°、流場の 1 マス 1.5m より十分緩い）
        float terraceSlopeMax = 20.0f;
        int   terraceTopMin = 5;         // 上面の一辺（マス）。2 段目がある時の奥行きは 2 段目に合わせて決まる
        int   terraceTopMax = 10;
        float terraceTier2Chance = 0.5f; // 2 段目を載せる確率（上面の幅が 5 マス以上の時）
        float terraceTier2HeightMin = 3.0f;
        float terraceTier2HeightMax = 4.5f;
        int   terraceClear = 5;          // 坂の麓の先に木・岩・台地・外周の崖を置かないマス数（1 以上）
                                         // （滑り降りた勢いのまま、すぐ何かにぶつからないように）

        // ---- 自然物（KayKit Forest）----
        // 木と岩は置物（1 マス以上を塞ぐ。周り 1 マスは他の置物を置かないので、
        // 並んで壁になることはない）。茂みは見た目だけ（衝突も格子も無し）。
        // 草は模型ではなく GrassRenderer（GPU の草の葉）
        int treeCount = 60;       // 林（ノイズで固まる）が主、所々に 1 本
        int rockCount = 20;
        int bushCount = 160;
        // これより小さい木・岩は見た目だけ（格子も衝突も無し。雑魚が間に入って震えたり角に詰まったりしないよう、
        // 道を塞ぐのは大きい物だけにする）。値は拡縮後の大きさ
        float treeBlockMinHeight = 3.0f;   // m（木の高さ）
        float rockBlockMinSize = 1.2f;     // m（岩の足跡の長い辺）

        // ---- 外周の崖 ----
        float wallHeight = 7.0f;
        // 外周の見た目を岩山にする（衝突・格子は崖の箱のまま、箱は描かない）。
        // 大きい岩（Res::Mdl::Forest::kCliffRocks）を拡大して外へ 3 列: 手前 8〜13m / 中 15〜22m / 奥 24〜34m
        bool  rockMountains = true;
        float mountainScale = 1.0f;       // 3 列の高さにまとめて掛ける

        // 玩家の初期地点（場地中央）の周りは平らに空ける（マス数の半径）
        int spawnClearRadius = 8;
    };

    // 床・外周・台地・坂道・高台を生成し、grid に占用と高さを登記する。
    // 生成した Entity は outTerrain に積む（シーンが破棄用に持つ）。
    // outGrassMask: 格子のマス毎に 1 = 草を生やす（GrassRenderer 用）。土の坂道・外周・登れない台地は 0
    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask = nullptr);

    // 床の色（線形の反照率。値ノイズの緑のむら + 所々の乾いた草）。草の色もこれに合わせる
    DirectX::SimpleMath::Vector4 GroundColor(float x, float z, uint32_t seed);
}
