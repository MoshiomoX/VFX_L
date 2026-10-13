// ============================================================
// TitleScreenUI.h
// タイトル画面の文字とメニュー（2026-10-09 作り直し。ユーザー：「タイトルとリザルトが不格好」）。
//   背景は戦闘シーンそのもの（TitleScene = CollisionTestScene のタイトルの設定。丘の上のプレイヤーをカメラが回る）。
//   ここは手前の物だけ：左側の暗い幕（SceneSnapshot::DrawBackdrop の scrim）・名前・一言・文字だけのメニュー・操作の案内。
//   起動直後は黒から 1.2 秒でふわっと出る。
// UI は戦闘と同じくシーンの HDR バッファに描く（色は線形の値）
// ============================================================
#pragma once
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/MenuList.h"
#include "UI/HowToPlayUI.h"
#include <memory>

class Graphics;
class Texture;

class TitleScreenUI
{
public:
    // 遊び方（kHowTo）はここで開いて閉じる（HowToPlayUI）。シーンには返さない
    enum Action { kNone = -1, kStart = 0, kTraining = 1, kHowTo = 2, kQuit = 3 };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, float screenW, float screenH);
    void Shutdown();
    void Layout(float screenW, float screenH);

    // 決まった項目（kNone = まだ）。決まった後は「読み込み中」になり、もう何も返さない
    int  Update(float dt);
    void Render(Graphics& gfx);

private:
    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    std::shared_ptr<Texture> m_White;
    MenuList       m_Menu;
    HowToPlayUI    m_HowTo;
    bool  m_Ready = false;

    float m_ScreenW = 1600.0f;
    float m_ScreenH = 900.0f;
    float m_Time = 0.0f;        // 出てからの秒（ふわっと出す）
    bool  m_Starting = false;   // 決定後（二重に送らない・「読み込み中」）
    float m_K = 1.0f;           // 文字・余白の倍率（画面短辺 / 900 × UI 全体の倍率）
    float m_Left = 0.0f;        // 左の余白（px）
};
