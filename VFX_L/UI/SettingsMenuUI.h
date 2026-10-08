// ============================================================
// SettingsMenuUI.h
// 一時停止メニューの「設定」ページ（2026-10-07 ユーザー：音量とアウトラインの ON / OFF をプレイヤーが選べるように）。
//   行 = 主音量 / 効果音 / 音楽 / UI の音（0〜100 のスライダー）、敵味方の縁取り（オン / オフ。10-08 からトゥーンの黒い線は消さない）、戻る。
//   キー   : W S / 上下で行を選ぶ、A D / 左右で値を変える（押しっぱなしで連続）、Enter で切替 / 戻る
//   パッド : 十字の上下左右、A で切替 / 戻る、B で戻る
//   マウス : 動かした時だけ行を選ぶ。◀ ▶ をクリックで 1 段、スライダーの上をクリック / ドラッグで直接
//   値は変えた瞬間に効く（音量 = AudioSystem、縁取り = GameSettings）。閉じる時に両方保存する。
//   画面が止まっている間も使うので dt は取らない（連続入力の猶予は固定値で減らす）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <vector>

class SpriteRenderer;
class TextRenderer;
class Texture;

class SettingsMenuUI
{
public:
    SettingsMenuUI();

    void Layout(float screenW, float screenH);

    // 開いた瞬間に呼ぶ（今の値を取り込み、カーソルを先頭へ）
    void Open();

    // 開いている間、毎フレーム呼ぶ。閉じる（戻る）なら true（保存はここで済ませる）
    bool HandleInput();

    // 外から閉じる時（GameUI が Esc を先に取った時）：変えた物があれば保存
    void Close();

    void Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white);

    // 一時停止メニューと同じ箱の大きさに合わせる（画面短辺に対する比率）
    float panelWidthRatio = 0.60f;
    float rowHeightRatio = 0.068f;
    float rowGapRatio = 0.012f;
    DirectX::SimpleMath::Vector4 dimColor = { 0.0f, 0.0f, 0.0f, 0.90f };
    DirectX::SimpleMath::Vector4 panelColor = { 0.0025f, 0.0020f, 0.0045f, 0.985f };

private:
    enum class Kind { Volume, Toggle, Back };
    struct Row
    {
        std::wstring label;
        Kind kind = Kind::Volume;
        int  bus = 0;      // Kind::Volume：AudioSystem::Bus
    };

    float GetValue(const Row& r) const;  // Volume 0〜1 / Toggle 0 or 1
    void  SetValue(const Row& r, float v);
    void  Step(int dir);                 // 選択中の行を 1 段動かす
    void  Activate();                    // Enter / A
    DirectX::SimpleMath::Vector2 RowPos(int i) const;
    // 右側の操作部（◀ 値 ▶ / スライダー）の矩形
    void ControlRect(int i, DirectX::SimpleMath::Vector2& pos, DirectX::SimpleMath::Vector2& size) const;

    std::vector<Row> m_Rows;
    int   m_Cursor = 0;
    float m_InputDelay = 0.0f;
    float m_RepeatDelay = 0.0f;          // 左右の押しっぱなし
    bool  m_Dragging = false;            // スライダーをマウスで掴んでいる
    bool  m_Dirty = false;               // 何か変えた（閉じる時に保存）
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };

    DirectX::SimpleMath::Vector2 m_Screen = { 1600.0f, 900.0f };
    DirectX::SimpleMath::Vector2 m_PanelPos = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_PanelSize = { 0.0f, 0.0f };
    float m_Short = 900.0f;
    float m_Scale = 1.0f;   // UI 全体の倍率（UIDeco::UIScale()）を画面に収まる所まで落とした値
    float m_RowsTop = 0.0f;
    float m_RowH = 0.0f;
    float m_Gap = 0.0f;
};
