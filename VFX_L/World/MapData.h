// ============================================================
// MapData.h
// 戦闘の地図 1 枚をデータにした物（2026-10-05、地図エディタの 1 歩目）。
//
// TerrainGenerator::Generate は seed から地形を決めながら建てる。その「建てた物」を全部ここへ記録し、
// TerrainGenerator::BuildFromMap が同じ建て方で並べ直す（乱数は使わない）= 保存した地図をそのまま読める。
//
// 中身は 2 種類:
//   土台 … マス毎の区域（平原 / 山頂 / 洞窟）・洞の岩の壁・起伏（ノード毎）・格子（通行 / 高さ場）・草を生やすマス
//   記録 … 衝突だけの箱 / 凸体、見た目の六面体（最後に 1 つのモデルへ合成）、置物（モデル + 位置）、塞いだマスの矩形
// 記録には kind（何の一部か）と group（同じ 1 個から出た物は同じ番号。木 = 見た目 + 衝突の箱 + 塞いだマス）を付ける。
// エディタはこの group 単位で消す・動かす（2 歩目以降）。
//
// ファイル（Assets/Data/MapData/<名前>.vmap）は小端の素のバイナリ。起伏と高さ場が大きい（約 2MB）ので JSON にしない
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>
#include <string>
#include <vector>

namespace MapData
{
    inline constexpr uint32_t kVersion = 6;   // 2 = placements、3 = volumes、4 = 地形の部品、5 = 区域の作り直し用の値、6 = 起伏の中身（丘の部品）。古い版も読める

    // 何の一部か（エディタの一覧・選別用）
    enum Kind : uint16_t
    {
        kFloor = 0,      // 床の縦の壁になる箱（平原・山頂・洞窟の底）
        kOuterWall,      // 外周の崖
        kSummitRamp,     // 山頂の長い坂
        kMineRamp,       // 洞窟の下り坂
        kCaveRoof,       // 洞の岩の壁・屋根・口の梁
        kTerrace,        // 高台（上面 + 坂 + 2 段目）
        kPlateau,        // 台地（本体 + 坂道 + 2 段目）
        kSkirt,          // 場外の地面
        kTree,
        kRock,
        kBush,
        kEdgeRock,       // 外周の岩山
        kRuinWall,       // 遺跡の壁・柱
        kTorch,
        kRoofRock,       // 洞の上に積んだ岩
        kManual,         // エディタで足した物
        kHill,           // 丘の部品（起伏に足す盛り上がり）
    };

    struct Tag { uint16_t kind = kFloor; uint32_t group = 0; };

    struct Box    { Tag tag; DirectX::SimpleMath::Vector3 lo, hi; uint32_t layer = 0; };   // 衝突だけ（軸平行）
    struct Hull   { Tag tag; DirectX::SimpleMath::Vector3 v[8]; uint32_t layer = 0; };     // 衝突だけ（凸体、世界座標の 8 頂点）
    struct Visual                                                                          // 見た目の六面体
    {
        Tag tag;
        DirectX::SimpleMath::Vector3 v[8];
        DirectX::SimpleMath::Vector4 top, side;
        int topLayer = -1, sideLayer = -1;   // TerrainSurface の層（-1 = 自動）
    };
    struct Prop                                                                            // 置物の見た目
    {
        Tag tag;
        int model = 0;                       // Map::models の番号
        DirectX::SimpleMath::Vector3 pos;    // 実体の位置（底合わせ済み）
        float yawDeg = 0.0f;
        float scale = 1.0f;                  // 一様（ファイルの単位込み）
    };
    struct Block  { Tag tag; int x = 0, z = 0, w = 0, d = 0; };                            // 塞いだマスの矩形

    struct Ramp   { DirectX::SimpleMath::Vector3 top, down; };

    // 機能付きの置き物（プレハブ。戦闘シーンが中身ごと作る：報酬の箱 = 金貨で開ける四択、Boss の門 = F で Boss）。
    // 1 つも無い種類は、戦闘シーンが従来通り seed から並べる
    enum PlaceType : uint16_t { kPlaceCrate = 0, kPlaceBossGate = 1 };
    struct Placement { uint16_t type = kPlaceCrate; DirectX::SimpleMath::Vector3 pos; float yawDeg = 0.0f; };   // pos = 底の中心（地面）

    // 手で置いた見えない体積（地図エディタの 3 歩目）。衝突の箱（solid）と塞ぐマス（blockMobs）はここから作る
    // （MapEdit::ApplyVolume。boxes / blocks に同じ group の記録として入り、戦闘シーンは他の記録と同じに建てる）。
    // 塞いだマスは雑魚が通れず、GPU の弾もそこで当たる
    struct Volume
    {
        Tag tag;                                  // kind = kManual
        DirectX::SimpleMath::Vector3 center;
        DirectX::SimpleMath::Vector3 half = { 2.0f, 1.5f, 2.0f };   // 軸平行
        bool solid = true;                        // プレイヤーがぶつかる（Layer_Prop：カメラの遮蔽は見ない）
        bool blockMobs = true;                    // 足跡のマスを塞ぐ
    };


    // ---- 地形の部品（版 4、地図エディタの 4 歩目）----
    // 台地・高台・坂は「部品」として持ち、衝突・見た目・高さ場・足元の台座はここから作り直せる（MapEdit）。
    // 同じ 1 個（台地 + 坂道 + 2 段目）は同じ group
    // 箱の部品（台地・高台の上面、2 段目）。足跡はマスの矩形
    struct BlockPart
    {
        Tag tag;
        int x = 0, z = 0, w = 1, d = 1;          // マス（左下と大きさ）
        float bottom = 0.0f, top = 0.0f;         // 箱の底（足元より少し埋めてある）と上面
        float base = 0.0f;                       // 足元の高さ（地面なら台座の高さ、2 段目なら下の段の上面）
        DirectX::SimpleMath::Vector4 topColor, sideColor;
        bool raise = true;                       // true = 上を歩ける（高さ場を上げる）。false = 登れないので格子を塞ぐ
        bool onGround = true;                    // 地面に載っている（足元に台座を作る）。2 段目は false
    };
    // 坂の部品（楔）。高い端が side の反対側、side の向きへ base まで下る
    struct RampPart
    {
        Tag tag;
        int x = 0, z = 0, w = 1, d = 1;          // マス
        uint8_t side = 0;                        // 下る向き：0 +x / 1 -x / 2 +z / 3 -z
        float base = 0.0f, top = 0.0f;
        DirectX::SimpleMath::Vector4 topColor, sideColor;
        bool grassy = false;                     // 草の坂（地面と同じ層、草を生やす）。false = 土の道
        bool onGround = true;                    // 足元に台座を作る（台地の上の坂・洞窟の中の坂は false）
        // どの箱に付いているか（同じ group の何番目の BlockPart か。-1 = 付いていない = 山頂・洞窟の坂）と、
        // その辺に沿った位置。箱の大きさを変えた時に足跡を作り直す用
        int owner = -1, offset = 0;
    };
    // 台座：起伏を高さ L（区域の基準からの差）に均す範囲。ノード（0.5m）単位、両端含む。m = 元の起伏へ戻す幅
    // 丘の部品（版 6）：起伏に足す 1 個の盛り上がり。高さ = height × (1 - (d / radius)^2)^2（負なら窪み）。
    // 生成が撒く土饅頭もこれ。エディタで選んで動かす・大きさを変える・消す
    struct Hill
    {
        Tag tag;                                 // kind = kHill
        float x = 0.0f, z = 0.0f;                // 世界
        float radius = 4.0f;
        float height = 1.0f;
    };
    // 起伏の全体の設定（版 6）：大きい丘（勾配ノイズ）と細かいうねり。面ごとの倍率を掛けた後の値
    struct ReliefParams
    {
        float hillHeight = 4.2f;                 // m（丘の振れ幅）
        float hillScale = 46.0f;                 // m（丘の大きさ = 波長）
        float detailHeight = 1.0f;               // m（細かいうねり）
        float detailScale = 18.0f;               // m
        float summitMul = 0.6f;                  // 山頂の上面の倍率
    };
    struct Pad
    {
        Tag tag;
        uint8_t zone = 0;                        // 0 平原 / 1 山頂
        int ax0 = 0, ax1 = 0, az0 = 0, az1 = 0, m = 1;
        float L = 0.0f;
    };
    struct Map
    {
        // ---- 見出し ----
        uint32_t seed = 0;
        int   biome = 0;
        int   gw = 0, gd = 0;                // マス
        bool  relief = false;                // 起伏あり（床のメッシュ・高さ場の衝突）
        int   reliefSubdiv = 1;
        bool  cave = false;                  // 洞窟に屋根がある
        float summitH = 0.0f, mineD = 0.0f, roofTopY = 0.0f;

        // ---- 土台 ----
        std::vector<uint8_t> zone;           // gw * gd：0 平原 / 1 山頂 / 2 洞窟
        std::vector<uint8_t> caveRing;       // gw * gd：洞の岩の壁
        std::vector<float>   reliefPlain;    // (gw * sub + 1) * (gd * sub + 1)：平原の起伏
        std::vector<float>   reliefSummit;   // 同：山頂の起伏（基準 summitH からの差）
        std::vector<uint8_t> walkable;       // gw * gd（生成が終わった時点。シーンの後処理の前）
        std::vector<float>   heights;        // (gw * sub) * (gd * sub)
        std::vector<uint8_t> grassMask;      // gw * gd

        // ---- 記録 ----
        std::vector<std::string> models;     // 置物のモデルのパス
        std::vector<Box>    boxes;
        std::vector<Hull>   hulls;
        std::vector<Visual> visuals;
        std::vector<Prop>   props;
        std::vector<Block>  blocks;
        std::vector<DirectX::SimpleMath::Vector3> torches;
        std::vector<Placement> placements;   // 報酬の箱・Boss の門
        std::vector<Volume> volumes;         // 手で置いた見えない体積

        // ---- 地形の部品（版 4）。これと素の起伏から、起伏・高さ場を作り直せる ----
        std::vector<float>     rawPlain, rawSummit;   // 台座で均す前の起伏（reliefPlain / reliefSummit と同じ大きさ。無ければ空）
        std::vector<BlockPart> blockParts;
        std::vector<RampPart>  rampParts;
        std::vector<Pad>       pads;
        int   padMargin = 5;                 // 台座の戻しの幅（マス。TerrainGenerator::Config と同じ）
        float reliefMaxSlopeDeg = 30.0f;     // 起伏の傾きの上限
        float rampSlopeDeg = 28.0f;          // エディタで足す坂道の既定の角度

        // ---- 区域（平原 / 山頂 / 洞窟）を塗り替えた時の作り直し用（版 5）----
        bool  hasZoneParams = false;         // 下の値が入っている（版 5 以降で生成 / 保存した地図）
        float floorPlainTop = 0.0f;          // 床の箱の上面（平原）。起伏の一番低い所より少し下
        float floorSummitTop = 0.0f;         // 同（山頂）
        float roofBottomY = 0.0f;            // 洞窟の屋根の下面
        float roofCollTop = 0.0f;            // 洞の岩の壁・屋根の衝突の上端（見えない高さまで）
        float roofRockMin = 8.0f, roofRockMax = 16.0f;   // 洞の上に積む岩の高さ（縁 → 真ん中）
        int   caveTorchSpacing = 6;          // 洞の中の松明の間隔（マス）
        bool  rimRock = false;               // 外周が岩山（松明を岩へ押し込む）
        float rimSink = 0.0f;                // 押し込む量
        std::vector<std::string> roofRockModels;   // 洞の上に積む岩のモデル
        std::string torchModel;              // 松明のモデル

        // ---- 起伏の中身（版 6）：素の起伏 = ノイズの丘（reliefParams）+ 丘の部品（hills）→ 傾きを抑える → + 筆の分（sculpt）----
        bool hasReliefParams = false;        // 下の値が入っている（版 6 以降で生成 / 保存した地図）
        ReliefParams reliefParams;
        std::vector<Hill> hills;
        std::vector<float> sculptPlain, sculptSummit;   // 起伏の筆で足した分（rawPlain と同じ大きさ。塗っていなければ空）

        // ---- 三層の結果（TerrainGenerator::Layout と同じ）----
        std::vector<int> summitCells, mineCells;
        bool hasMineDeep = false;
        DirectX::SimpleMath::Vector3 mineDeep;
        std::vector<Ramp> summitRamps, mineRamps;

        uint32_t nextGroup = 1;              // 次に振る group（エディタが足す時もここから）

        int ModelIndex(const std::string& path);   // 無ければ足す
        void Clear() { *this = Map{}; }
    };

    // 小端の素のバイナリへ / から。Load は版・大きさが合わなければ false
    void Serialize(const Map& map, std::vector<uint8_t>& out);
    bool Deserialize(const std::vector<uint8_t>& in, Map& map);

    // Assets/Data/MapData/<name>.vmap
    std::string PathFor(const std::string& name);
    bool Save(const std::string& name, const Map& map);
    bool Load(const std::string& name, Map& map);
    std::vector<std::string> List();     // 保存してある地図の名前（拡張子なし）

    // エディタ（F6）の「この地図で遊ぶ」：空でなければ、次に始まる戦闘シーンがこの名前の地図を読む
    std::string& PlayOverride();
}
