// ============================================================
// TerrainGenerator.h
// 戦闘の地形を seed から生成する（格子に沿った「台地と坂道」の野原）。
//
// 場地の三層（2026-10-02、用户：山頂・平原・鉱洞の大きな分層。200m 四方）:
//   平原（高さ 0。開局の場所。下の台地・高台・自然物はここ）を中心に、対角の隅に
//   山頂（+summitHeight の大きな台地。長い坂が数本、残りは崖 = 雑魚は飛び降りる）と
//   鉱洞（-mineDepth の窪地。下り坂が数本、縁から飛び降りられる）。
//   床はマス毎の高さ（level）の段々のメッシュ 1 つ、衝突は高さ毎の箱の組。
//
// 生成するもの:
//   床（草地。色むらのある段々のメッシュ）+ 外周の崖
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
    // 面の見た目（2026-09-30。第 1 面 草原 / 第 2 面 砂漠 / 第 3 面 遺跡）。
    // 地形の形は全部共通で、床・台地の色、自然物の表、外周（岩山 or 遺跡の壁）、草の有無が変わる
    enum class Biome { Grassland = 0, Desert = 1, Dungeon = 2 };

    struct Config
    {
        uint32_t seed = 1;
        Biome biome = Biome::Grassland;

        // ---- 場地の三層（山頂・平原・鉱洞）----
        // 隅の組（どちらの対角か、どちらが山頂か）は seed で決まる。
        // 形 = 隅の正方形（一辺 size マス、外周の崖の内側から）+ 内側の二辺の出っ張り・凹み + 内側の角の面取り。
        // 2026-10-02 は 300m の場地で作り、10-03 に 200m へ戻した（用户：高低差はそのまま、水平だけ 2/3）。
        // 坂も長さが 2/3 になったので急になった（山頂 18〜22° → 26〜31°、鉱洞 22° → 31°。雑魚は 40°、流場は 1 マス 1.5m まで）
        bool  layers = true;
        int   summitSize = 33;            // マス（66m）
        float summitHeight = 16.0f;       // m
        int   summitRamps = 3;            // 平原へ下りる長い坂の数（場所が無ければ減る）
        int   summitRampWidth = 4;        // マス（8m）
        float summitRampSlopeMin = 26.0f; // 度（滑り込みで平原まで滑り降りる）
        float summitRampSlopeMax = 31.0f;
        int   mineSize = 33;
        float mineDepth = 10.0f;          // m
        int   mineRamps = 2;              // 平原から底へ下りる坂の数
        int   mineRampWidth = 2;          // マス（4m）
        float mineRampSlope = 31.0f;      // 度
        int   mineRockCount = 13;         // 鉱洞の底に足す岩（木は生やさない）

        // ---- 鉱洞の屋根（2026-10-03、用户：推奨どおり）----
        // 坑を岩の塊で覆う（外から見ると山の麓の洞穴、入口は下り坂の上端）。坑の周り 1 マスは塞いだ岩の壁、
        // 坑の上は roofBottom〜roofTop の板、上に大きい岩を積んで低い山に見せる（真ん中ほど高い）。
        // 衝突は roofCollisionTop まで（見た目より高い = 跳んでも上に乗れない）。
        // 中は暗いので壁に松明（caveTorchSpacing マス毎。点光源は場面が近い物にだけ付ける）
        bool  mineRoof = true;
        float roofBottom = 5.0f;          // 屋根の下面（平原から m。坑の底から 15m、口の高さ 5m）
        float roofTop = 12.0f;            // 岩の塊の上面（見た目。平原から m）
        float roofCollisionTop = 40.0f;   // 衝突の上端（見えない）
        float roofRockMin = 8.0f;         // 上に積む岩の高さ（縁 → 真ん中で roofRockMax まで）。
        float roofRockMax = 16.0f;        // 縁も高めにして、外から見た輪郭を箱でなく岩山にする
        int   caveTorchSpacing = 6;       // マス（12m）

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
        int treeCount = 60;       // 林（ノイズで固まる）が主、所々に 1 本（鉱洞には生やさない）
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

    // 三層の結果（箱・Boss の門の置き場所を決める用）。マスは gz * 格子の幅 + gx
    struct Layout
    {
        std::vector<int> summitCells;   // 山頂の上面の歩けるマス
        std::vector<int> mineCells;     // 鉱洞の底の歩けるマス（坂は含まない）
        bool hasMineDeep = false;
        DirectX::SimpleMath::Vector3 mineDeep;   // 鉱洞の一番奥（坂の降り口から歩いて一番遠い底のマス。地面の高さ）
        // 坂：上端の辺の中央（地面の高さ）と下る向き（xz の単位）
        struct Ramp { DirectX::SimpleMath::Vector3 top, down; };
        std::vector<Ramp> summitRamps, mineRamps;
    };

    // 床・外周・台地・坂道・高台を生成し、grid に占用と高さを登記する。
    // 生成した Entity は outTerrain に積む（シーンが破棄用に持つ）。
    // outGrassMask: 格子のマス毎に 1 = 草を生やす（GrassRenderer 用）。土の坂道・外周・登れない台地・鉱洞は 0
    // outTorches: 遺跡の壁の松明の位置（場面が近い物に点光源を付ける）。他の面では空
    // outLayout: 山頂・鉱洞のマス（layers = false なら空）
    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask = nullptr,
        std::vector<DirectX::SimpleMath::Vector3>* outTorches = nullptr, Layout* outLayout = nullptr);

    // 床の色（線形の反照率。草原 = 値ノイズの緑のむら + 所々の乾いた草、砂漠 = 砂丘の縞、遺跡 = 石畳）。
    // 草の色もこれに合わせる
    DirectX::SimpleMath::Vector4 GroundColor(float x, float z, uint32_t seed, Biome biome = Biome::Grassland);
}
