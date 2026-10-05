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
    };
    Check Verify(const MapData::Map& map);
}
