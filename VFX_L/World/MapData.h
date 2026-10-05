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
    inline constexpr uint32_t kVersion = 3;   // 2 = 機能付きの置き物（placements）、3 = 手で置いた体積（volumes）。古い版も読める

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
