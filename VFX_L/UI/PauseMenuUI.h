// ============================================================
// PauseMenuUI.h
// 一時停止のメニュー（再開 / やり直す / タイトルへ）。
//
// 開閉は GameUI が決める（P / パッド Back。Esc は Window.cpp で終了に使われているので使わない）。
// ここは開いている間の入力と描画だけ。項目の操作は MenuList、
// パッドの B は「再開」の近道。
// 決定した結果は Action で返し、シーンの切替はシーン側に任せる
// ============================================================
#pragma once
#include "UI/MenuList.h"
#include "UI/SettingsMenuUI.h"
#include <SimpleMath.h>
#include <memory>

class SpriteRenderer;
class TextRenderer;
class Texture;

class PauseMenuUI
{
public:
    enum class Action { None, Resume, Restart, Title };

    PauseMenuUI();

    void Layout(float screenW, float screenH);

    // 開いた瞬間に呼ぶ（カーソルを先頭へ、押しっぱなしで即決しない猶予）
    void Open() { m_List.Open(); m_InSettings = false; }

    // 開いている間、毎フレーム呼ぶ（「設定」のページを開いている間はそちらへ渡す）
    Action HandleInput();

    void Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white);

    // 「設定」のページを開いているか。Esc はまず GameUI が見るので、
    // 開いている時はメニュー全体を閉じずにこのページだけ閉じる（CloseSettings）
    bool IsInSettings() const { return m_InSettings; }
    void CloseSettings();
    void TestOpenSettings() { m_InSettings = true; m_Settings.Open(); }   // TEMP-TEST: 自動テスト ui が設定のページを撮る

    // ---- 見た目（画面短辺に対する比率）----
    float panelWidthRatio = 0.46f;
    float itemHeightRatio = 0.075f;
    float itemGapRatio = 0.018f;
    // 線形の値（戦闘の UI は HDR バッファに描かれてガンマを通る。0.9 で画面では 6 割ほど暗くなる）
    DirectX::SimpleMath::Vector4 dimColor = { 0.0f, 0.0f, 0.0f, 0.90f };
    DirectX::SimpleMath::Vector4 panelColor = { 0.0025f, 0.0020f, 0.0045f, 0.985f };
    DirectX::SimpleMath::Vector4 accentColor = { 1.0f, 0.80f, 0.35f, 1.0f };

private:
    MenuList m_List;
    SettingsMenuUI m_Settings;
    bool m_InSettings = false;

    DirectX::SimpleMath::Vector2 m_Screen = { 1600.0f, 900.0f };
    DirectX::SimpleMath::Vector2 m_PanelPos = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_PanelSize = { 0.0f, 0.0f };
    float m_Short = 900.0f;
    float m_Scale = 1.0f;   // UI 全体の倍率（UIDeco::UIScale()）を画面に収まる所まで落とした値。Layout で決める
};
