// ============================================================
// MenuList.h
// 縦に並んだボタンの一覧（一時停止のメニュー・タイトルで共用）。
//   マウス : 動かした時だけカーソルを奪い、クリックで決定
//   キー   : W S / 上下で移動、Enter / Space で決定
//   パッド : 十字の上下で移動、A で決定
// 画面が止まっている間も使うので dt は取らない（猶予は固定値で減らす）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <vector>

class SpriteRenderer;
class TextRenderer;
class Texture;

class MenuList
{
public:
    void SetItems(std::vector<std::wstring> labels) { m_Labels = std::move(labels); }
    int  Count() const { return (int)m_Labels.size(); }

    // 一番上の項目の左上・1 項目の大きさ・項目の間
    void Layout(const DirectX::SimpleMath::Vector2& topLeft,
        const DirectX::SimpleMath::Vector2& itemSize, float gap);
    float Height() const;

    // 開いた瞬間に呼ぶ（カーソルを先頭へ、押しっぱなしの決定キーで即決しない猶予）
    void Open();

    // 決まった項目の番号。まだなら -1
    int HandleInput();

    void Draw(SpriteRenderer& sprite, TextRenderer& text,
        const std::shared_ptr<Texture>& white, float textScale) const;

    int Cursor() const { return m_Cursor; }

    DirectX::SimpleMath::Vector4 accentColor = { 1.0f, 0.80f, 0.35f, 1.0f };

private:
    DirectX::SimpleMath::Vector2 ItemPos(int i) const;

    std::vector<std::wstring> m_Labels;
    DirectX::SimpleMath::Vector2 m_TopLeft = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_ItemSize = { 200.0f, 48.0f };
    float m_Gap = 12.0f;

    int   m_Cursor = 0;
    float m_InputDelay = 0.0f;
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };
};
