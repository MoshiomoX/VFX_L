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

    // 決まった項目の番号。まだなら -1。
    // dt = 実時間の秒（開いた直後の猶予を減らす。止まっている間も渡す）
    int HandleInput(float dt = 0.016f);

    void Draw(SpriteRenderer& sprite, TextRenderer& text,
        const std::shared_ptr<Texture>& white, float textScale) const;

    int Cursor() const { return m_Cursor; }

    DirectX::SimpleMath::Vector4 accentColor = { 1.0f, 0.80f, 0.35f, 1.0f };

    // 見た目（2026-10-09）。Panels = 暗い地 + 古金の枠のボタン（一時停止）、
    // Text = 文字だけの左寄せの一覧。選んでいる項目は右へ少しずれて明るくなり、左に菱形・後ろに金の光・下に線（タイトル）
    enum class Style { Panels, Text };
    Style style = Style::Panels;
    float fade = 1.0f;   // Text の見た目の濃さ（0〜1。タイトルの出始めにふわっと出す）

private:
    DirectX::SimpleMath::Vector2 ItemPos(int i) const;
    int HandleInputRaw(float dt);   // 判定だけ（HandleInput が音を付ける）
    void DrawTextStyle(SpriteRenderer& sprite, TextRenderer& text,
        const std::shared_ptr<Texture>& white, float textScale) const;

    // Text の見た目の選択の度合い（0〜1、項目ごと。描く時に実時間で寄せる）
    mutable std::vector<float> m_SelAnim;
    mutable float m_AnimClock = -1.0f;

    std::vector<std::wstring> m_Labels;
    DirectX::SimpleMath::Vector2 m_TopLeft = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_ItemSize = { 200.0f, 48.0f };
    float m_Gap = 12.0f;

    int   m_Cursor = 0;
    float m_InputDelay = 0.0f;
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };
};
