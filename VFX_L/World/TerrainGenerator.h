// ============================================================
// TerrainGenerator.h
// 戦闘の地形を seed から生成する（格子に沿った「台地と坂道」の野原）。
//
// フィールドの三層（2026-10-02、ユーザー：山頂・平原・洞窟の大きな分層。200m 四方）:
//   平原（高さ 0。開始時の場所。下の台地・高台・自然物はここ）を中心に、対角の隅に
//   山頂（+summitHeight の大きな台地。長い坂が数本、残りは崖 = 雑魚は飛び降りる）と
//   洞窟（-mineDepth の窪地。下り坂が数本、縁から飛び降りられる）。
//   床はマス毎の高さ（level）の段々のメッシュ 1 つ、衝突は高さ毎の箱の組。
//   2026-10-04 から平原と山頂の上面に起伏（緩い丘 + 土饅頭、Config::relief）。床のメッシュは頂点毎の高さ、
//   歩く面の衝突は高さ場（ColliderShape::HeightField）、箱は崖の縦の壁だけ。台地などの足元は台座で均す
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
namespace MapData { struct Map; }

namespace TerrainGenerator
{
    // 面の見た目（2026-09-30。第 1 面 草原 / 第 2 面 砂漠 / 第 3 面 遺跡）。
    // 地形の形は全部共通で、床・台地の色、自然物の表、外周（岩山 or 遺跡の壁）、草の有無が変わる
    enum class Biome { Grassland = 0, Desert = 1, Dungeon = 2 };

    struct Config
    {
        uint32_t seed = 1;
        Biome biome = Biome::Grassland;

        // ---- フィールドの三層（山頂・平原・洞窟）----
        // 隅の組（どちらの対角か、どちらが山頂か）は seed で決まる。
        // 形 = 隅の正方形（一辺 size マス、外周の崖の内側から）+ 内側の二辺の出っ張り・凹み + 内側の角の面取り。
        // 2026-10-02 は 300m のフィールドで作り、10-03 に 200m へ戻した（ユーザー：高低差はそのまま、水平だけ 2/3）。
        // 坂も長さが 2/3 になったので急になった（山頂 18〜22° → 26〜31°、洞窟 22° → 31°。雑魚は 40°、フローフィールドは 1 マス 1.5m まで）
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
        int   mineRampWidth = 4;          // マス（8m。10-03 に 2 → 4：Boss（高さ 4.8m・幅約 3m）が洞から出られる幅）
        float mineRampSlope = 31.0f;      // 度
        int   mineRockCount = 7;          // 洞窟の底に足す岩（木は生やさない。10-03 に 13 → 7）

        // ---- 洞窟の屋根（2026-10-03、ユーザー：推奨どおり）----
        // 坑を岩の塊で覆う（外から見ると山の麓の洞窟、入口は下り坂の上端）。坑の周り 1 マスは塞いだ岩の壁、
        // 坑の上は roofBottom〜roofTop の板、上に大きい岩を積んで低い山に見せる（真ん中ほど高い）。
        // 衝突は roofCollisionTop まで（見た目より高い = 跳んでも上に乗れない）。
        // 中は暗いので壁に松明（caveTorchSpacing マス毎。点光源はシーンが近い物にだけ付ける）
        bool  mineRoof = true;
        float roofBottom = 8.0f;          // 屋根の下面（平原から m。坑の底から 18m、口の高さ 8m。10-03 に 5 → 8：Boss の頭がつかえない）
        float roofTop = 15.0f;            // 岩の塊の上面（見た目。平原から m。屋根の厚さ 7m は roofBottom と一緒に上げた）
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
        float rampSlopeDeg = 28.0f;    // 30° 以下（雑魚は 40° まで、フローフィールドは 1 マス 1.5m まで通す）

        // ---- 高台（一面だけが長い坂、残り三面は崖）----
        // 台地より先に置く（場所を取るので後回しにすると入らない）。坂の向きは高台ごとにランダム。
        // 坂の長さ = 高さ / tan(角度)：6m・18° で約 18m
        int   terraceCount = 4;
        float terraceHeightMin = 5.0f;
        float terraceHeightMax = 7.0f;
        float terraceSlopeMin = 16.0f;   // 度（雑魚の 40°、フローフィールドの 1 マス 1.5m より十分緩い）
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
        // 草はモデルではなく GrassRenderer（GPU の草の葉）
        // 2026-10-03 に半分へ（ユーザー：フィールドの木・岩・茂みを今の半分ほどに）。以前は 60 / 20 / 160
        int treeCount = 30;       // 林（ノイズで固まる）が主、所々に 1 本（洞窟には生やさない）
        int rockCount = 10;
        int bushCount = 80;
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
        // 一番手前の列（2026-10-03、ユーザー：外周の岩に入り込めてめり込みする。少し外へ下げてから衝突を付ける）。
        // 衝突の箱 = 岩と一緒に回した包囲箱の edgeRockShrink 倍（岩は角が丸いので少し小さく）。
        // 箱の内側の面がフィールドの縁から edgeRockIntrudeMin〜Max m 内に入る所に置く（負 = 縁より外）。
        // 縁より内に入る岩だけ衝突（凸体、Layer_Prop = カメラの射線は見ない）を付け、箱が 1/4 以上掛かるマスは塞ぐ（雑魚用）。
        // 縁より外の岩と高い所は外周の崖の箱（見えない壁）が止める
        float edgeRockIntrudeMin = -0.6f;
        float edgeRockIntrudeMax = 1.0f;
        float edgeRockShrink = 0.85f;
        // 遺跡の壁の柱：内に入る量（柱の奥行きに対する割合）。衝突（AABB）を付ける
        float ruinColumnIntrude = 0.4f;

        // プレイヤーの初期地点（フィールド中央）の周りは平らに空ける（マス数の半径）
        int spawnClearRadius = 8;

        // ---- 起伏（2026-10-04、ユーザー：純平地をやめて緩い丘 + 小さい土饅頭。推奨値）----
        // 平原と山頂の上面: 大きい丘（波長 hillScale m、±hillHeight m）+ 細かいうねり + 土饅頭。洞窟の底は平ら。
        // 台地・坂・高台の足元は「台座」で局所の平均の高さに均し（縁 1 マス + 3 マスで元の起伏へ戻る）、
        // 構造物はその高さに載る。洞窟の周り（岩の壁・洞の口）は平原の 0 に均す。
        // 面ごとの倍率: 砂漠 1.25（砂丘）、遺跡 0.5（石畳）
        // 2026-10-05 ユーザー「起伏をもう少し大きく、坂ももう少し急に」→ 丘 2.2 → 3.8m（高低差 4 → 7〜8m）、
        // 土饅頭も大きく。上限は雑魚の 40°・フローフィールドの 1 マス 1.5m（≒ 37°）
        bool  relief = true;
        float hillHeight = 4.2f;          // m（丘の振れ幅。勾配ノイズ ≒ ±1 × これ）
        float hillScale = 46.0f;          // m（丘の大きさ）
        float hillDetailHeight = 1.0f;    // m（細かいうねり）
        float hillDetailScale = 18.0f;    // m
        float bumpCount = 70.0f;          // 土饅頭の数（200m 四方あたり）
        float bumpRadiusMin = 3.0f;       // m（直径 6〜12m）
        float bumpRadiusMax = 6.0f;
        float bumpHeightMin = 0.8f;       // m
        float bumpHeightMax = 2.0f;
        float bumpMaxSlope = 0.33f;       // 高さ ≤ 半径 × これ（一番急な所 ≒ 27°。雑魚は 40° まで、プレイヤーは 60° まで歩ける）
        int   padMargin = 5;              // 台座の芯から元の起伏へ戻すマス数（起伏が大きいほど長くしないと境が急になる）
        float reliefMaxSlopeDeg = 30.0f;  // 隣のノードとの傾きの上限（軸方向。素の起伏は -2°、斜めは最大 √2 倍 ≒ 39°）
        float summitReliefMul = 0.6f;     // 山頂の上面は控えめ
        int   reliefSubdiv = 2;           // 地面のメッシュの細かさ（1 マス 2m を n × n に割る）
    };

    // 三層の結果（箱・Boss の門の置き場所を決める用）。マスは gz * 格子の幅 + gx
    struct Layout
    {
        std::vector<int> summitCells;   // 山頂の上面の歩けるマス
        std::vector<int> mineCells;     // 洞窟の底の歩けるマス（坂は含まない）
        bool hasMineDeep = false;
        DirectX::SimpleMath::Vector3 mineDeep;   // 洞窟の一番奥（坂の降り口から歩いて一番遠い底のマス。地面の高さ）
        // 坂：上端の辺の中央（地面の高さ）と下る向き（xz の単位）
        struct Ramp { DirectX::SimpleMath::Vector3 top, down; };
        std::vector<Ramp> summitRamps, mineRamps;
    };

    // 床・外周・台地・坂道・高台を生成し、grid に占有と高さを登録する。
    // 生成した Entity は outTerrain に積む（シーンが破棄用に持つ）。
    // outGrassMask: 格子のマス毎に 1 = 草を生やす（GrassRenderer 用）。土の坂道・外周・登れない台地・洞窟は 0
    // outTorches: 遺跡の壁の松明の位置（シーンが近い物に点光源を付ける）。他の面では空
    // outLayout: 山頂・洞窟のマス（layers = false なら空）
    // outMap: 建てた物の記録（MapData。保存すれば BuildFromMap で同じ地図を読める）
    void Generate(Registry& reg, ID3D11Device* device, GridWorld& grid,
        const Config& cfg, std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask = nullptr,
        std::vector<DirectX::SimpleMath::Vector3>* outTorches = nullptr, Layout* outLayout = nullptr,
        MapData::Map* outMap = nullptr);

    // BuildFromMap が建てる部分（エディタは見た目だけ欲しい：kPartVisuals | kPartGround）
    enum BuildParts : uint32_t { kPartColliders = 1, kPartVisuals = 2, kPartProps = 4, kPartGround = 8, kPartAll = 15 };

    // 記録（保存した地図）から同じ物を建てる。乱数は使わない。格子の大きさが合わなければ false（何も作らない）。
    // outMap: 建て直した物をもう一度記録する（読んだ地図と同じ内容になる。確認・編集の元）。map と同じ物は渡さない
    bool BuildFromMap(Registry& reg, ID3D11Device* device, GridWorld& grid, const MapData::Map& map,
        std::vector<Entity>& outTerrain, std::vector<uint8_t>* outGrassMask = nullptr,
        std::vector<DirectX::SimpleMath::Vector3>* outTorches = nullptr, Layout* outLayout = nullptr,
        MapData::Map* outMap = nullptr, uint32_t parts = kPartAll);

    // 床の色（線形のアルベド。草原 = 値ノイズの緑のむら + 所々の乾いた草、砂漠 = 砂丘の縞、遺跡 = 石畳）。
    // 草の色もこれに合わせる
    DirectX::SimpleMath::Vector4 GroundColor(float x, float z, uint32_t seed, Biome biome = Biome::Grassland);
}
