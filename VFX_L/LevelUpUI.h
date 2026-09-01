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
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SpriteRenderer;
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

    void Draw(SpriteRenderer& sprite, const LevelComponent& lv);

    // ---- レイアウト（すべて比率）----
    float cardWidthRatio = 0.20f;   // カード幅（画面短辺基準）
    float cardAspect = 1.45f;   // 高さ / 幅
    float cardGapRatio = 0.04f;   // カード間の隙間
    float centerY = 0.50f;   // 画面高さに対する中心位置

    DirectX::SimpleMath::Vector4 dimColor = { 0.0f, 0.0f, 0.0f, 0.72f };
    DirectX::SimpleMath::Vector4 cardColor = { 0.16f, 0.14f, 0.20f, 0.96f };
    DirectX::SimpleMath::Vector4 hoverColor = { 0.32f, 0.30f, 0.42f, 0.98f };

    // 選択中の index（キーボード / パッド用）
    int GetCursor() const { return m_Cursor; }

private:
    DirectX::SimpleMath::Vector2 CardPosition(int index, int total) const;
    DirectX::SimpleMath::Vector2 CardSize() const;
    std::shared_ptr<Texture> GetIcon(ItemID id) const;

    std::shared_ptr<Texture> m_BlockTex;
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