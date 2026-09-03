// ============================================================
// SpellbookUI.h
// 魔法書。習得済みでグリッド未配置のブロックが物理で箱に溜まる。
//
// データは持たない。箱の中身は毎フレームの差分同期で決まる:
//   あるべき数 = SpellbookComponent の所持数 - グリッド配置数
//                - （自分の箱から掴んでドラッグ中なら 1）
//   多ければ消し、足りなければ箱の上から降らせる。
//   グリッドから抜けば勝手に降ってくるし、掴んで置き損ねれば
//   勝手に降って戻る。どちらの UI も相手に通知しない。
//
// 物理は内製の 2D（Registry / PhysicsSystem とは無関係）:
//   形状は円（見た目は occupyCells で描く）。
//   円にした理由は、回転する異形同士の衝突を書かないため。
//   接触点が円心から外れると力矩が生まれ、回転は衝突から自然に出る。
//   固定ステップ 1/120 で積分（コマ落ちでも床を抜けない）。
//
// 掴んだ瞬間に body を消して DragContext へ引き渡す。
//   角度と縮小率は visAngle / visScale に入れ、Update が 0 / 1 へ寄せる
//   （「雑然と積まれた物を、正して持ち上げる」の見た目）。
// ============================================================
#pragma once
#include "SpellID.h"
#include "UI/DragContext.h"
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SpriteRenderer;
class Texture;
class Registry;
struct SpellbookComponent;
struct BackpackComponent;

class SpellbookUI
{
public:
    void Initialize(std::shared_ptr<Texture> blockTex);
    void LoadIcons();
    void Layout(float screenW, float screenH);

    void SetDragContext(DragContext* drag) { m_Drag = drag; }

    // グリッドとマス寸を揃える（拖拽中に大きさが跳ばないように）
    void SetCellSize(float cellSize) { m_CellSize = cellSize; }

    // 同期 → 物理 → 掴み判定。Backpack 層が開いている間だけ呼ばれる
    void Update(const SpellbookComponent& book, const BackpackComponent& bp, float dt);

    void Draw(SpriteRenderer& sprite);

    // ---- 見た目の調整（ImGui から触る）----
    float boxScreenRatio = 0.42f;    // 箱の内寸が画面短辺に占める割合
    float marginRatio = 0.02f;    // 画面端からの距離
    float wallRatio = 0.05f;    // 内寸に対する壁の厚さ
    float boxScale = 0.6f;     // 箱の中でのブロック縮小率

    // ---- 物理の調整 ----
    float gravity = 1800.0f;   // px/s^2
    float restitution = 0.25f;     // 反発（小さめ。跳ねすぎると収納に見えない）
    float spinTransfer = 0.4f;      // 接線速度をどれだけ回転に変えるか

    DirectX::SimpleMath::Vector4 frameColor = { 0.35f, 0.20f, 0.06f, 1.00f };
    DirectX::SimpleMath::Vector4 innerColor = { 0.72f, 0.35f, 0.33f, 1.00f };

    int GetBodyCount() const { return (int)m_Bodies.size(); }

private:
    // ============================================================
    // 箱の中の1個。真値ではなく所持数の視覚表現。
    // pos は円の中心。描画はこの中心に occupyCells の AABB を
    // 重ねて angle で回す（物理は円、見た目は本来の形）
    // ============================================================
    struct BodyState
    {
        ItemID id = ItemID::Fireball;
        DirectX::SimpleMath::Vector2 pos = { 0, 0 };
        DirectX::SimpleMath::Vector2 vel = { 0, 0 };
        float radius = 20.0f;
        float angle = 0.0f;    // rad
        float angVel = 0.0f;    // rad/s

    };

    void SyncBodies(const SpellbookComponent& book, const BackpackComponent& bp);
    void StepPhysics(float dt);
    void TryGrab();
    void SpawnBody(ItemID id);
    float ComputeRadius(ItemID id) const;

    std::shared_ptr<Texture> m_BlockTex;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;
    std::shared_ptr<Texture> GetIcon(ItemID id) const;

    DragContext* m_Drag = nullptr;

    std::vector<BodyState> m_Bodies;
    float m_PhysAccum = 0.0f;

    // 箱の内寸（Layout で確定。壁の内側）
    DirectX::SimpleMath::Vector2 m_BoxMin = { 0, 0 };
    DirectX::SimpleMath::Vector2 m_BoxMax = { 0, 0 };
    float m_Wall = 12.0f;

    float m_CellSize = 56.0f;   // グリッドと同じマス寸（フルサイズ）
};