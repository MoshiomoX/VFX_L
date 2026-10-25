// ============================================================
// ShopScene.h
// 面と面の間の商店（2026-10-15。金貨の使い道の第二歩。ユーザー「まず商店を作る、売る物は適当に少し」）。
//   リザルトで「次のステージへ」を押すと（クリアして次の面がある時だけ）ここへ来て、
//   「次のステージへ」で戦闘へ。1 回のプレイで最大 2 回（第 1 → 2 面、第 2 → 3 面）。
//   相手は面をまたぐ引き継ぎ（g_RunCarry）：金貨を払って魔法書へ入れる / 能力値をその場で上げる。
//   画面：背景はリザルトと同じ写真（SceneSnapshot）。左に品揃えの一覧（6 行）、右に選んでいる品の
//   カード（レベルアップの四択と同じ LevelUpUI::DrawCard）、右上に所持金貨、一覧の下に
//   買う / 品揃えを変える / 次のステージへ の 3 つのボタン（キー・パッド・マウス）
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/CameraBase.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/LevelUpUI.h"
#include "UI/ShopStock.h"
#include "UI/GameUI.h"
#include "ECS/Registry.h"
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <vector>

class Texture;

class ShopScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    enum Btn { kBuy = 0, kReroll = 1, kLeave = 2, kBag = 3, kButtonCount = 4 };   // kBag = 右上（金貨の下）
    struct Button
    {
        std::wstring label;
        std::wstring key;   // <キーボード|パッド> の絵（InputGlyph）
        DirectX::SimpleMath::Vector2 pos;
        DirectX::SimpleMath::Vector2 size;
    };

    void Layout();
    void BuildButtons();
    void TryBuy();
    void TryReroll();
    void Leave();
    // バックパック（2026-10-15 ユーザー：買った物をその場で置けるように）。
    // 戦闘の GameUI をそのまま使う：この場面だけの Registry にバックパックと魔法書を持つ entity を 1 つ作り、
    // 開く時に引き継ぎ（g_RunCarry）から写し、閉じた時に書き戻す
    void OpenBackpack();
    void CloseBackpack();
    void SetMessage(const std::wstring& s, bool good);
    std::shared_ptr<Texture> GetIcon(ItemID id) const;
    // 一覧の行の名前・種別（ItemInfo::Describe は品の id だけで決まるので、品毎に 1 回だけ作って覚える）
    struct RowText { ItemID id; std::wstring title; std::wstring category; };
    const RowText& GetRowText(ItemID id);
    DirectX::SimpleMath::Vector2 RowPos(int i) const;
    void DrawRows();
    void DrawGold();
    void DrawButtons();

    // TEMP-TEST: VFXL_SHOP_AUTOTEST=1（Core/Game.cpp が偽の引き継ぎを積んでここへ直行）。
    // 買う → 枠を買う → 引き直す → 足りない買い物 → 戦闘へ、の順に autotest.log へ書く
    void AutoTest();
    void TestLog(const char* what);

    CameraBase     m_Camera;
    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    std::shared_ptr<Texture> m_White;
    std::shared_ptr<Texture> m_GoldIcon;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;
    std::vector<RowText> m_RowTexts;   // GetRowText の覚え（返した参照は次に呼ぶまでだけ使う。足すと動くので）
    LevelUpUI m_Cards;    // 右のカード（描くだけ。入力はこのシーンが持つ）
    ShopStock m_Stock;
    Registry  m_Registry;     // バックパックの画面用（entity 1 つだけ）
    Entity    m_Player = {};
    GameUI    m_UI;
    bool      m_BagOpen = false;
    bool      m_BagJustClosed = false;   // 閉じたフレームの Tab を開く操作として拾わない

    int m_Cursor = 0;         // 選んでいる行
    int m_HoverRow = -1;
    int m_HoverButton = -1;
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };
    float m_InputDelay = 0.0f;

    // レイアウト（Layout で画面から決める）
    float m_ScreenW = 1600.0f;
    float m_ScreenH = 900.0f;
    float m_K = 1.0f;
    bool  m_LayoutPad = false;
    DirectX::SimpleMath::Vector2 m_ListPos, m_ListSize;
    float m_RowH = 60.0f;
    float m_RowGap = 6.0f;
    float m_ListPad = 16.0f;
    DirectX::SimpleMath::Vector2 m_CardPos;
    Button m_Buttons[kButtonCount];

    std::wstring m_Msg;        // 一覧の下の一言（買った / 足りない）
    float m_MsgTime = 0.0f;
    bool  m_MsgGood = true;
    float m_GoldFlash = 0.0f;  // 金貨が減った瞬間の光
    int   m_LastCoins = 0;

    float m_Time = 0.0f;
    bool  m_Leaving = false;
    bool  m_AutoTest = false;
    int   m_AutoStep = 0;
};
