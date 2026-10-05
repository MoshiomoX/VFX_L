// ============================================================
// BackpackPadControl.h
// バックパック画面のパッド操作（2026-10-05）。
//
// スティックでマウスカーソルを真似ない（多マスの形は狙いが合わない）。
// マスに吸い付くカーソルで「掴む / 置く」を行う:
//   左スティック / 十字 : カーソルを 1 マスずつ（押し続けると連続）
//   A  : 掴む / 置く（置けない所では置かない）
//   B  : 掴んでいれば取り消し（元の位置へ）、無ければバックパックを閉じる
//   LB / RB : 回転
//   X  : カーソルの物（掴んでいればその物）を魔法書へ戻す
//   Y  : グリッド ⇔ 魔法書の箱 の切り替え
// 魔法書の箱は物理で積まれていてマスが無いので、位置の近い物へ順に選択を移し、
// A で取り出すとグリッドへ戻る。
//
// 仕組み：掴む・運ぶ・置くは BackpackUI のドラッグ処理をそのまま使う。
//   カーソルのマスの中心を「仮のマウス位置」として渡すだけなので、
//   置ける / 置けないの判定・影・影響マスの表示はマウスと同じ物になる。
//
// 最後に触った機器がパッドの間だけ働く（マウスを動かすと戻る）。切り替えは GameUI が見る
// ============================================================
#pragma once
#include "UI/DragContext.h"
#include <cstdint>
#include <SimpleMath.h>

class SpriteRenderer;
class TextRenderer;
class BackpackUI;
class SpellbookUI;
struct BackpackComponent;

class BackpackPadControl
{
public:
    enum class Focus { Grid, Book };

    // 最後に触った機器を見る。パッドなら true。バックパックが一番上の間、毎フレーム最初に呼ぶ
    bool DetectDevice();
    bool IsActive() const { return m_Active; }

    // 入力を処理する。戻り値 = 閉じる要求（何も掴まずに B）
    bool Update(BackpackComponent& bp, BackpackUI& grid, SpellbookUI& book, DragContext& drag, float dt);

    // カーソルと操作の案内。BackpackUI::Draw の後に呼ぶ
    void Draw(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid, const DragContext& drag,
        const DirectX::SimpleMath::Vector2& screen) const;

    // 説明カード：カーソルを止めて少し経ったら出す。出す位置は指している物のそば
    bool TooltipReady() const { return m_Rest >= tooltipDelay; }
    DirectX::SimpleMath::Vector2 TooltipAnchor() const { return m_TooltipAnchor; }

    Focus GetFocus() const { return m_Focus; }
    int Row() const { return m_Row; }
    int Col() const { return m_Col; }
    uint32_t BookSelection() const { return m_BookSel; }

    // ---- 調整 ----
    float repeatDelay = 0.28f;      // 押し続けて連続移動が始まるまで（秒）
    float repeatInterval = 0.09f;   // 連続移動の間隔
    float tooltipDelay = 0.3f;

    // TEMP-TEST: 自動テスト（VFXL_BATTLE_AUTOTEST=padbag）。次の Update 1 回分の入力をパッドの代わりに入れる
    enum TestButton : unsigned { kA = 1, kB = 2, kX = 4, kY = 8, kLB = 16, kRB = 32 };
    void TestPress(int dx, int dy, unsigned buttons) { m_Test = { dx, dy, buttons }; m_HasTest = true; }

private:
    struct Input { int dx = 0, dy = 0; unsigned buttons = 0; };
    Input Read(float dt);

    bool UpdateGrid(const Input& in, BackpackComponent& bp, BackpackUI& grid, DragContext& drag);
    bool UpdateBook(const Input& in, BackpackComponent& bp, BackpackUI& grid, SpellbookUI& book, DragContext& drag);
    uint32_t PickBookNeighbor(const SpellbookUI& book, int dx, int dy) const;

    bool  m_Active = false;
    Focus m_Focus = Focus::Grid;
    int   m_Row = 4, m_Col = 4;     // グリッドのカーソル
    uint32_t m_BookSel = 0;         // 箱の中で選んでいる物（SpellbookUI の uid。0 = 無し）
    float m_Rest = 0.0f;            // カーソルが止まっている時間
    DirectX::SimpleMath::Vector2 m_TooltipAnchor = { 0.0f, 0.0f };

    // 押し続けの連続移動
    int   m_HeldX = 0, m_HeldY = 0;
    float m_RepeatTimer = 0.0f;

    Input m_Test;
    bool  m_HasTest = false;
};
