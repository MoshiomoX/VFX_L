// ============================================================
// MapTerrainEdit.h
// 地図のデータ（MapData::Map）の地形の部品（台地・高台・坂）の編集（2026-10-05、地図エディタの 4 歩目の 1 段目）。
//
// 地形の部品（BlockPart / RampPart）と素の起伏（rawPlain / rawSummit）から、
//   起伏（台座で均した面）・高さ場・通行・草のマス・部品の衝突と見た目の記録
// を作り直す。作り方は生成（TerrainGenerator）と同じ関数（TerrainBuild）を通る。
// 生成した直後の地図に Rederive を掛けても、起伏は 1 ビットも変わらない（自動テスト mapedit が確かめる）。
//
// 版 3 以前の地図（部品の記録が無い）では使えない（HasParts が false）
// ============================================================
#pragma once
#include "World/MapData.h"
#include <SimpleMath.h>
#include <string>

namespace MapTerrainEdit
{
    // 部品を編集できる地図か（素の起伏がある = 版 4 以降で保存 / 生成した物）
    bool HasParts(const MapData::Map& map);

    // 起伏・高さ場・通行・草のマスを、部品と台座から全部作り直す（部品を変えた後に呼ぶ）
    void Rederive(MapData::Map& map);

    // group の部品の衝突・見た目・塞ぐマスの記録を作り直す（部品の中身を変えた後。Rederive の前に呼ぶ）
    void RegenRecords(MapData::Map& map, uint32_t group);
    // group の台座を部品から作り直す（位置・大きさを変えた後）
    void RegenPads(MapData::Map& map, uint32_t group);

    // group を (dx, dz) マス動かす。足元の高さは動かした先の地面（この group の台座を除いた起伏の平均）に合わせ直す。
    // 記録・台座・起伏・高さ場まで作り直す。場外へ出るなら動かさず false
    bool MoveGroup(MapData::Map& map, uint32_t group, int dx, int dz);
    // 部品を変えた後のまとめ（足元の合わせ直し → 坂の足跡 → 記録 → 台座 → Rederive）
    void Refresh(MapData::Map& map, uint32_t group);
    void DeleteGroup(MapData::Map& map, uint32_t group);

    // 台地を足す（w x d マス、高さ height m、上を歩ける）。新しい group を返す
    uint32_t AddPlateau(MapData::Map& map, int x, int z, int w, int d, float height);
    // group の ownerOrdinal 番目の箱へ坂道を付ける（side = 下る向き 0〜3、辺に沿って offset から幅 width マス）。
    // 長さは map.rampSlopeDeg から。付けられたら true
    bool AddRamp(MapData::Map& map, uint32_t group, int ownerOrdinal, int side, int offset, int width);

    // group の n 番目の箱 / 坂（無ければ nullptr）
    MapData::BlockPart* Block(MapData::Map& map, uint32_t group, int ordinal);
    MapData::RampPart*  Ramp(MapData::Map& map, uint32_t group, int ordinal);
    int BlockCount(const MapData::Map& map, uint32_t group);
    int RampCount(const MapData::Map& map, uint32_t group);
    // 世界の点（xz）にある部品の group（上にある箱を優先。無ければ 0）
    uint32_t GroupAt(const MapData::Map& map, float x, float z);

    // ---- 区域（平原 / 山頂 / 洞窟）の塗り替え（4 歩目の 2 段目）----
    // 区域を塗り替えられる地図か（版 5 以降で生成 / 保存した物）
    bool CanEditZones(const MapData::Map& map);
    // マス (cx, cz) を中心に半径 radius マスの円を zone（0 平原 / 1 山頂 / 2 洞窟）で塗る。外周の崖のマスは塗らない。
    // 変えたマスの数を返す。区域の表を書き換えるだけなので、塗り終わったら RegenZones を呼ぶ
    int PaintZone(MapData::Map& map, int cx, int cz, int radius, uint8_t zone);
    // 区域の形から決まる物を全部作り直す：床の箱、洞の岩の壁・屋根、三層の結果（山頂・洞窟のマス、一番奥 = Boss の門の候補）、
    // 起伏・高さ場・通行・草のマス（Rederive）。regenProps = 洞の上の岩と洞の中の松明も置き直す（乱数は地図の seed から）
    void RegenZones(MapData::Map& map, bool regenProps = true);
    // 山頂の長い坂（summit = true。草の坂、麓へ下る）/ 洞窟の下り坂（土の坂、坑の底へ下る）を足す。
    // (x, z) = 足跡の左下のマス、side = 下る向き、width = 幅、length = 長さ（マス）。新しい group を返す。
    // 区域の縁に合わせるのは置く人（山頂の坂は高い端を山頂の縁に、洞窟の坂は高い端を坑の縁の内側に）
    uint32_t AddZoneRamp(MapData::Map& map, bool summit, int x, int z, int side, int width, int length);

    // 確認用：生成器の結果と作り直した結果の差（生成した直後の地図で呼ぶ）
    struct Check
    {
        bool reliefSame = false;       // 起伏が 1 ビットも違わない
        float heightMaxDiff = 0.0f;    // 高さ場の差の最大（m）
        int   heightDiffCells = 0;     // 1mm 以上違う高さマスの数
        std::string detail;            // 違うマスの最初の数個（高さマスの番号・生成の値・作り直した値）
        bool walkableSame = false;
        bool grassSame = false;
        bool recordsSame = false;      // 部品の衝突・見た目の記録を作り直しても中身が同じ（順番は問わない）
        bool zonesSame = false;        // 区域から作り直した床・洞の壁 / 屋根の記録、岩の壁のマス、三層の結果が同じ
    };
    Check Verify(const MapData::Map& map);
}

// 実装の中だけで使う（MapTerrainEdit.cpp / MapZoneEdit.cpp）
namespace MapTerrainEdit::detail
{
    std::string& LastPerf();                        // TEMP-TEST: 直前の Rederive の内訳（ms）
    uint64_t RecordSum(const MapData::Map& map);    // 衝突・見た目・塞ぐマスの記録の中身の合計（順番に依らない）
    bool ZonesMatch(const MapData::Map& map);       // 区域から作り直した物が今の記録と同じか（Verify 用）
}
