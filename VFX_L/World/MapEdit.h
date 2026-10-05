// ============================================================
// MapEdit.h
// 地図のデータ（MapData::Map）への編集操作（2026-10-05、地図エディタの 2 歩目）。
//
// エディタの画面（Scene/MapEditMode）からも戦闘シーンからも使える、データだけの操作。
// 物は group 単位で扱う（木 1 本 = 見た目 + 衝突の箱 + 塞いだマス）。
// 動かす・回す・消す度に、その物の衝突と塞いだマスを作り直し、通行の表（walkable）を塞いだマスの記録から組み直す
// ============================================================
#pragma once
#include "World/MapData.h"
#include <SimpleMath.h>

namespace MapEdit
{
    using DirectX::SimpleMath::Vector3;

    // 置物の衝突の付け方
    enum class Collision
    {
        None,        // 見た目だけ（茂み・小さい岩）
        Trunk,       // 幹だけ（木。0.6m 角の柱、根元の 1 マスを塞ぐ）
        Footprint,   // 足跡いっぱい（岩。回した包囲箱 × 0.85、掛かるマスを塞ぐ）
    };

    // 地面の高さ（地図の高さ場を双線形で。GridWorld::SampleHeight と同じ式）
    float GroundHeight(const MapData::Map& map, float x, float z);

    // 通行の表を「塞いだマスの記録」から組み直す（記録の編集の後に呼ぶ）
    void RebuildWalkable(MapData::Map& map);
    // 今の通行の表が記録から組み直した物と同じか（生成した地図で成り立つことの確認用）
    bool WalkableMatchesBlocks(const MapData::Map& map);

    // group の記録を数える・探す
    int  FindProp(const MapData::Map& map, uint32_t group);     // 最初の置物の番号（無ければ -1）
    // 置物 1 個だけの物（木・岩・茂み・エディタで足した物）か。回す・拡縮・衝突の付け替えができるのはこれだけ
    bool IsSimpleProp(const MapData::Map& map, uint32_t group);
    Collision CollisionOf(const MapData::Map& map, uint32_t group);

    // 置物 1 個の物の衝突と塞いだマスを、今の位置・向き・大きさから作り直す。
    // lo / hi = モデルの包囲箱（モデルの座標。Prop::scale を掛けると m）
    void SetPropCollision(MapData::Map& map, uint32_t group, Collision mode, const Vector3& lo, const Vector3& hi);

    // group をまとめて動かす（置物・衝突・近くの松明の灯り）。塞いだマスは衝突の足跡から作り直す
    void MoveGroup(MapData::Map& map, uint32_t group, const Vector3& delta);
    void DeleteGroup(MapData::Map& map, uint32_t group);

    // ---- 手で置いた見えない体積（MapData::Volume。3 歩目）----
    int  FindVolume(const MapData::Map& map, uint32_t group);   // 無ければ -1
    // 足す。新しい group を返す（衝突・塞ぐマスも作る）
    uint32_t AddVolume(MapData::Map& map, const Vector3& center, const Vector3& half, bool solid, bool blockMobs);
    // 体積の中身（位置・大きさ・solid・blockMobs）を変えた後に呼ぶ：衝突の箱と塞ぐマスを作り直す
    void ApplyVolume(MapData::Map& map, uint32_t group);

    // 置物を足す（kind = kManual など）。新しい group を返す。衝突は SetPropCollision で付ける
    uint32_t AddProp(MapData::Map& map, const std::string& model, const Vector3& pos, float yawDeg, float scale,
        MapData::Kind kind = MapData::kManual);
}
