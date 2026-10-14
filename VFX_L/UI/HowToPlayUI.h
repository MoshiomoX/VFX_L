// ============================================================
// HowToPlayUI.h
// 「遊び方」のページ（2026-10-13 ユーザー：「新手引導を作って、ゲームの機能を紹介して」→ 初回のチュートリアル
//   （TutorialGuide）と、いつでも読める説明のページの 2 本立て。これは後者）。
//   タイトルのメニューと一時停止のメニューの「遊び方」から開く。3 ページ（操作 / 魔法とバックパック / 戦い方）。
//   キー   : A D / 左右でページ、Enter / Space で次（最後なら閉じる）、Esc で閉じる
//   パッド : 十字の左右 / LB RB でページ、A で次、B で閉じる
//   マウス : 左クリックで次
//   文字の中の <キーボード|パッド> と {K:W} {A} などは InputGlyph が絵にする（今使っている機器の方だけ出る）。
//   画面が止まっている間も使うので dt は取らない（猶予は固定値で減らす）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <vector>

class SpriteRenderer;
class TextRenderer;
class Texture;

class HowToPlayUI
{
public:
    HowToPlayUI();

    void Layout(float screenW, float screenH);

    // 開いた瞬間に呼ぶ（1 ページ目へ）
    void Open();
    bool IsOpen() const { return m_Open; }

    // 開いている間、毎フレーム呼ぶ。閉じたら true
    bool HandleInput();
    void Close() { m_Open = false; }

    void Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white);

    int PageCount() const { return (int)m_Pages.size(); }
    int Page() const { return m_Page; }

    DirectX::SimpleMath::Vector4 dimColor = { 0.0f, 0.0f, 0.0f, 0.90f };
    DirectX::SimpleMath::Vector4 panelColor = { 0.0025f, 0.0020f, 0.0045f, 0.985f };

private:
    struct PageData
    {
        std::wstring title;
        std::vector<std::wstring> lines;   // 「・」で始まる行は項目、空行は間
    };
    void Flip(int dir);

    std::vector<PageData> m_Pages;
    int   m_Page = 0;
    bool  m_Open = false;
    float m_InputDelay = 0.0f;

    DirectX::SimpleMath::Vector2 m_Screen = { 1600.0f, 900.0f };
    DirectX::SimpleMath::Vector2 m_PanelPos = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_PanelSize = { 0.0f, 0.0f };
    float m_Short = 900.0f;
    float m_Scale = 1.0f;   // UI 全体の倍率（UIDeco::UIScale()）を画面に収まる所まで落とした値
};
