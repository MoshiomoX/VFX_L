// ============================================================
// TitleScreenUI.h
// タイトル画面の文字とメニュー。
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
#include "UI/TitleLogo.h"
#include "UI/TitleSpellTable.h"
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

    // 決まった項目（kNone = まだ）。決まった後は「読み込み中」になり、もう何も返さない。
    // はじめる / トレーニングは名前が吹き飛ぶのを見せてから返す（kStartDelay 秒後）
    int  Update(float dt);
    void Render(Graphics& gfx);

    // 景色の場面の切替の暗転（0〜1）。幕の上・名前とメニューの下に黒を重ねる
    void SetCurtain(float a) { m_Curtain = a; }
    int  MenuCursor() const { return m_Menu.Cursor(); }
    void TestPick(int item);   // TEMP-TEST: その項目を選んだ扱い（名前が吹き飛ぶ）
    // 右下の魔法の表が組み替える背嚢（景色のプレイヤーの物。毎フレーム入れる）・組み替えたか・マウスが表の上か
    void SetBackpack(BackpackComponent* bp) { m_TableBp = bp; }
    bool ConsumeSpellEdited() { const bool e = m_SpellEdited; m_SpellEdited = false; return e; }
    bool MouseOverTable() const { return m_Table.MouseOver(); }
    const TitleSpellTable& TestTable() const { return m_Table; }   // TEMP-TEST

private:
    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    TextRenderer   m_TitleText;   // ゲーム名専用（Res::Fnt::Title = Mystery Quest、ASCII のみ）
    TextRenderer   m_MenuText;    // メニューと「読み込み中」の字体（Res::Fnt::Menu = Zen Antique）
    TitleLogo      m_Logo;        // 名前の小片（マウス・そよ風で散る）
    TitleSpellTable m_Table;      // 右下の魔法の表
    BackpackComponent* m_TableBp = nullptr;
    bool  m_SpellEdited = false;
    DirectX::SimpleMath::Vector2 m_LogoOrigin = { 0.0f, 0.0f };   // 最後に描いた名前の左上
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };
    DirectX::SimpleMath::Vector2 m_MouseVel = { 0.0f, 0.0f };     // px/秒（少しならした値）
    float m_Curtain = 0.0f;
    int   m_PendingPick = -1;     // 決めた項目（名前が吹き飛び終わるまで返さない）
    float m_PickTimer = 0.0f;
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
