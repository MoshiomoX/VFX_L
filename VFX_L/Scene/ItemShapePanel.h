// ============================================================
// ItemShapePanel.h
// アイテムの形（占有マス・影響マス）の編集 + 試し置きのバックパック。
// 投射物エディタの 3 頁目（Item Shapes）。
//
// 形の編集:
//   アイテムを選び、そのアイテムの形を ImGui のマス目で塗る。
//     左クリック / なぞり = 占有マス、右クリック / なぞり = 影響マス、Ctrl + 左 = そこをアンカーにする
//   マス目の真ん中がアンカー (0,0)（回転の中心・魔法書から掴む点）。
//   塗った瞬間に ItemDatabase の形が変わり、試し置きのバックパックも Refit で置き直す。
//   Save でそのアイテムのデータ（Assets/Data/ItemData/<名前>.json）へ書く。次の起動からはこれが使われる。
//
// 試し置き（別窓）:
//   本番と同じ BackpackLogic で置き、本番と同じ BackpackAggregateSystem で集約し、
//   本番と同じ WeaponSystem で標的へ撃つ（弾はエディタの SwarmSystem を飛ぶ）。
//   機能をコードで書いてあるアイテムなら、形を変えた時の効き方をここでそのまま試せる。
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/System/BackpackAggregateSystem.h"
#include "ECS/System/WeaponSystem.h"
#include "Collider/CollisionSystem.h"
#include "Item/ItemTypes.h"
#include "SpellID.h"
#include <SimpleMath.h>
#include <string>
#include <vector>

class SwarmSystem;
class AreaVFXPlayer;
struct VFXContext;
struct BackpackComponent;

class ItemShapePanel
{
public:
    void Init(SwarmSystem* swarm, AreaVFXPlayer* areaVFX, const VFXContext* vfxCtx);

    // 頁を開いた瞬間に呼ぶ。他の頁で弾の値が変わっているかもしれないので集約し直す
    void OnActivate();

    // 頁が開いている間だけ呼ぶ。試し置きのバックパックを集約し、Fire が入っていれば撃つ
    void Update(float dt, const DirectX::SimpleMath::Vector3& muzzle);

    void DrawTab();     // 頁の中身: アイテムの一覧・マス目・アイテム箱・保存
    void DrawBench();   // 別窓: 試し置きのバックパックと集約結果

private:
    // ---- 形の編集 ----
    void DrawItemList();
    void DrawShapeGrid();
    void DrawShapeTools();
    // 編集中のアイテムの形を差し替える（ItemDatabase + 試し置きの置き直し + 未保存の印）
    void ApplyEdit(const std::vector<CellOffset>& occupy, const std::vector<CellOffset>& influence);
    bool IsUnsaved(ItemID id) const;
    void SetUnsaved(ItemID id, bool unsaved);
    bool SaveItem(ItemID id);

    // ---- 試し置き ----
    BackpackComponent& Bench();
    void FillFrames();          // キャンバス全面に枠を敷く（形を試す邪魔をしないように）
    void ResetToGameStart();    // 本番の開始と同じ（中央に 3x3 の枠だけ）
    void DrawBenchGrid();
    void DrawBenchResult();

    // ---- 形の編集 ----
    ItemID m_Item = ItemID::Fireball;   // 塗っているアイテム
    std::vector<ItemID> m_Unsaved;      // 塗ったがまだ保存していないアイテム
    int  m_PreviewRot = 0;          // マス目の表示だけ回す（塗りは回転を戻して形へ書く）
    int  m_PaintLayer = -1;         // なぞり塗り中の層（0 = 占有 / 1 = 影響 / -1 = 塗っていない）
    bool m_PaintValue = false;      // なぞり塗りで書く値（最初のマスを反転した値）
    int  m_LastPaintR = 999;        // 同じマスを何度も塗らないように
    int  m_LastPaintC = 999;
    int  m_Preset = 0;              // 占有マスの雛形（Apply で差し替え）
    std::string m_Status;           // 保存・読込の結果

    // ---- 試し置き ----
    Registry m_Reg;                 // 試し置き専用（術者 1 体だけ）
    Entity   m_Caster = EntityTraits::NULL_ENTITY;
    BackpackAggregateSystem m_Aggregate;
    WeaponSystem    m_Weapon;
    CollisionSystem m_NoCollision;  // エリートは居ないので空のまま。WeaponSystem の索敵に渡すだけ
    ItemID m_BenchItem = ItemID::Fireball;
    int    m_BenchRot = 0;
    bool   m_Fire = true;
    int    m_BenchEvicted = 0;      // 形を変えて置けなくなり、手元へ戻った数（累計）
};
