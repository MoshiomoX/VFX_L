#pragma once
// ============================================================
// TitleSpellTable.h
// タイトル画面の右下の魔法の表：景色の中のプレイヤーが撃っている背嚢をそのまま小さく見せ、
// マウスで組み替えられる（並べ方は戦闘のバックパックと同じ BackpackUI）。
//   上に魔法・ルーンの候補を並べ、そこからドラッグして置く。盤から外へドラッグ / 右クリックで外す。ホイール / R で回す。
//   置いた魔法はその場で景色のプレイヤーの杖へ（集約は BackpackAggregateSystem が dirty を見てやる）
// ============================================================
#include "UI/BackpackUI.h"
#include "UI/DragContext.h"
#include "SpellID.h"
#include <SimpleMath.h>
#include <memory>
#include <vector>

class SpriteRenderer;
class TextRenderer;
class Texture;
struct BackpackComponent;

class TitleSpellTable
{
public:
    void Initialize();
    // 右下へ置く。k = UI の倍率
    void Layout(float screenW, float screenH, float k);

    // 毎フレーム。bp = 景色のプレイヤーの背嚢（null なら何もしない）。中身が変わったら true
    bool Update(BackpackComponent* bp);
    void Draw(SpriteRenderer& sprite, TextRenderer& text, const BackpackComponent* bp, float fade);

    // マウスが表の上（タイトルの他の操作・マウスの風を止める）
    bool MouseOver() const { return m_MouseOver || m_Drag.IsActive(); }

    // TEMP-TEST: 候補の絵の真ん中・盤のマスの真ん中（画面の px）
    bool TestPaletteCenter(ItemID id, DirectX::SimpleMath::Vector2& out) const;
    DirectX::SimpleMath::Vector2 TestCellCenter(int row, int col) const;

private:
    BackpackUI m_Grid;
    DragContext m_Drag;
    std::vector<ItemID> m_Palette;      // 候補（攻撃魔法・範囲魔法・召喚物・ルーン）
    DirectX::SimpleMath::Vector2 m_PanelMin, m_PanelMax;   // 表全体（候補 + 盤）
    DirectX::SimpleMath::Vector2 m_PaletteOrigin;          // 候補の左上
    float m_Icon = 30.0f;               // 候補 1 つの一辺（px）
    int   m_Columns = 9;
    float m_K = 1.0f;
    int   m_HoverPalette = -1;
    bool  m_MouseOver = false;
    bool  m_Ready = false;

    bool PaletteAt(const DirectX::SimpleMath::Vector2& p, int& index) const;
    DirectX::SimpleMath::Vector2 PaletteCell(int index) const;
};
