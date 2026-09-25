// ============================================================
// LevelUpUI.h
// レベルアップ時の習得候補を提示して選ばせる。
//
// レイアウトは BackpackUI と同じく画面短辺に対する比率で持つ。
// 解像度が変わっても崩れないようにするため。
//
// 入力を握っている間はゲームが止まっている前提で書いている。
// 選び終わるまで他の操作を受け付けない。
// ============================================================
#pragma once
#include "SpellID.h"
#include "UI/ItemSheetView.h"
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SpriteRenderer;
class TextRenderer;
class Texture;
struct LevelComponent;

class LevelUpUI
{
public:
    void Initialize(std::shared_ptr<Texture> blockTex);
    void LoadIcons();
    void Layout(float screenW, float screenH);

    // 選ばれた ID を返す。まだ選ばれていなければ false。
    bool HandleInput(const LevelComponent& lv, ItemID& outPicked);

    // text は能力値のカード（形が無い物）の文字に使う。SpriteRenderer の後に描かれる
    void Draw(SpriteRenderer& sprite, TextRenderer& text, const LevelComponent& lv);

    // ---- レイアウト（すべて比率）----
    float cardWidthRatio = 0.30f;   // カード幅（画面短辺基準）。名前・説明・能力値が入る幅
    float cardAspect = 1.50f;   // 高さ / 幅
    float cardGapRatio = 0.035f;  // カード間の隙間

    // カードの文字（幅 270px の時の大きさ。実際はカード幅に比例させる）
    ItemSheetView::Style textStyle;
    float centerY = 0.50f;   // 画面高さに対する中心位置

    DirectX::SimpleMath::Vector4 dimColor = { 0.0f, 0.0f, 0.0f, 0.72f };
    // 画面に出る時は明るく持ち上がるので（深い色が灰色に見える）、暗めの値にしておく
    DirectX::SimpleMath::Vector4 cardColor = { 0.030f, 0.026f, 0.045f, 0.96f };
    DirectX::SimpleMath::Vector4 hoverColor = { 0.075f, 0.068f, 0.110f, 0.98f };

    // 選択中の index（キーボード / パッド用）
    int GetCursor() const { return m_Cursor; }

    // 区切り線などに使う無地の白（無ければブロックの貼图で代用）
    void SetWhiteTexture(std::shared_ptr<Texture> tex) { m_WhiteTex = tex; }

private:
    DirectX::SimpleMath::Vector2 CardPosition(int index, int total) const;
    DirectX::SimpleMath::Vector2 CardSize() const;
    std::shared_ptr<Texture> GetIcon(ItemID id) const;
    void DrawShapePreview(SpriteRenderer& sprite, const ItemCommon& c,
        const DirectX::SimpleMath::Vector2& areaPos,
        const DirectX::SimpleMath::Vector2& areaSize) const;

    std::shared_ptr<Texture> m_BlockTex;
    std::shared_ptr<Texture> m_WhiteTex;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;

    DirectX::SimpleMath::Vector2 m_ScreenSize = { 1600.0f, 900.0f };
    float m_CardW = 300.0f;
    float m_CardH = 435.0f;
    float m_CardGap = 60.0f;

    int m_Cursor = 0;      // キーボード操作用のカーソル
    int m_Hover = -1;      // マウスが乗っているカード

    // 表示された直後に押しっぱなしの入力で確定してしまうのを防ぐ猶予
    float m_InputDelay = 0.0f;
    bool  m_WasChoosing = false;
};