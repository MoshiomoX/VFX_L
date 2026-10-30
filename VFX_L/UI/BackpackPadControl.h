// ============================================================
// BackpackPadControl.h
// バックパック画面のパッド操作。
//
// 2026-10-28 からスティックで動かすカーソル（以前はマスに吸い付くカーソルだった。
// ユーザー「パッドはカーソル操作に、取り消しは要らない、閉じるのはメニューのボタン、X は加速、B は素早く外す」）:
//   左スティック : カーソルを動かす（倒した量で速さが変わる）
//   X（押している間）: カーソルが速くなる
//   十字 : 1 マスずつ（グリッドの上ならマスの真ん中に揃う。押し続けると連続）
//   A  : 掴む / 置く（グリッドの上で置けない所では置かない。グリッドの外なら魔法書へ戻して、その場から落とす）
//   B  : 素早く外す = カーソルの下の物（掴んでいればその物）を魔法書へ戻す
//   LB / RB : 回転
//   Y  : カーソルをグリッド ⇔ 魔法書の箱 へ飛ばす
//   閉じる = Start（Tab と同じ。GameUI が見る）。取り消しは無い
// 魔法書の箱の物もカーソルを乗せて A で掴む（マウスと同じ）。
//
// 仕組み：掴む・運ぶ・置くは BackpackUI のドラッグ処理をそのまま使う。
//   カーソルの位置を「仮のマウス位置」として渡すだけなので、
//   置ける / 置けないの判定・影・影響マスの表示はマウスと同じ物になる。
//
// 最後に触った機器がパッドの間だけ働く（マウスを動かすと戻る）。切り替えは GameUI が見る
// ============================================================
#pragma once
#include "UI/DragContext.h"
#include <cstdint>
#include <SimpleMath.h>

class SpriteRenderer;
class TextRenderer;
class BackpackUI;
class SpellbookUI;
struct BackpackComponent;

class BackpackPadControl
{
public:
    // 最後に触った機器を見る。パッドなら true。バックパックが一番上の間、毎フレーム最初に呼ぶ
    bool DetectDevice();
    bool IsActive() const { return m_Active; }

    // 入力を処理する。screen = 画面の大きさ（カーソルの速さと動ける範囲）
    void Update(BackpackComponent& bp, BackpackUI& grid, SpellbookUI& book, DragContext& drag, float dt,
        const DirectX::SimpleMath::Vector2& screen);

    // 指しているマスの枠と操作の案内。BackpackUI::Draw の後に呼ぶ
    void Draw(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid, const DragContext& drag,
        const DirectX::SimpleMath::Vector2& screen) const;
    // カーソルの矢印。説明カードより上に出すため、GameUI が一番最後（DrawOverlay の後ろ）に呼ぶ
    void DrawPointer(SpriteRenderer& sprite, const BackpackUI& grid, const DirectX::SimpleMath::Vector2& screen) const;

    // マウスで操作している間の案内（パッドの案内と同じ場所）
    static void DrawMouseHint(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid, const DragContext& drag,
        const DirectX::SimpleMath::Vector2& screen);

    // 説明カード：カーソルを止めて少し経ったら出す。出す位置はカーソルのそば
    bool TooltipReady() const { return m_Rest >= tooltipDelay; }
    DirectX::SimpleMath::Vector2 TooltipAnchor() const { return m_TooltipAnchor; }

    DirectX::SimpleMath::Vector2 Cursor() const { return m_Cursor; }
    int HoverRow() const { return m_HoverRow; }     // カーソルの下のマス（グリッドの外 = -1）
    int HoverCol() const { return m_HoverCol; }
    uint32_t BookSelection() const { return m_BookSel; }   // カーソルの下の箱の物（0 = 無し）

    // ---- 調整 ----
    float cursorSpeed = 11.0f;      // 倒し切った時の速さ（マス/秒。画面の大きさ・UI 倍率に依らない）
    float cursorCurve = 2.0f;       // 倒した量の何乗で速さを決めるか（少し倒すと細かく動く）
    float boostMul = 2.2f;          // X を押している間の速さの倍率
    float repeatDelay = 0.28f;      // 十字：押し続けて連続移動が始まるまで（秒）
    float repeatInterval = 0.09f;   // 十字：連続移動の間隔
    float tooltipDelay = 0.3f;

    // TEMP-TEST: 自動テスト（VFXL_BATTLE_AUTOTEST=padbag）。次の Update 1 回分の入力をパッドの代わりに入れる。
    // dx / dy = 十字の 1 マス、stick = 左スティック（その 1 フレームだけ）
    enum TestButton : unsigned { kA = 1, kB = 2, kX = 4, kY = 8, kLB = 16, kRB = 32 };
    void TestPress(int dx, int dy, unsigned buttons,
        const DirectX::SimpleMath::Vector2& stick = { 0.0f, 0.0f })
    {
        m_Test = { dx, dy, buttons, stick };
        m_HasTest = true;
        m_TestMode = true;
    }
    // TEMP-TEST: カーソルをその場所へ置く
    void TestCursor(const DirectX::SimpleMath::Vector2& p) { m_Cursor = p; m_HasCursor = true; }

private:
    struct Input
    {
        int dx = 0, dy = 0;          // 十字の 1 マス（このフレームに進む分）
        unsigned buttons = 0;        // 押した瞬間
        DirectX::SimpleMath::Vector2 stick = { 0.0f, 0.0f };   // 左スティック（上 = +y）
        bool boost = false;          // X を押している
    };
    Input Read(float dt);

    void MoveCursor(const Input& in, const BackpackUI& grid, float dt, const DirectX::SimpleMath::Vector2& screen);
    bool InGridRect(const BackpackUI& grid) const;
    static void DrawHintBar(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid, const wchar_t* hint, float k);

    bool  m_Active = false;
    bool  m_HasCursor = false;      // まだ一度も置いていない = 最初の Update でグリッドの真ん中へ
    DirectX::SimpleMath::Vector2 m_Cursor = { 0.0f, 0.0f };      // 画面 px
    DirectX::SimpleMath::Vector2 m_GridReturn = { 0.0f, 0.0f };  // Y で箱へ飛んだ時のグリッドの位置（戻り先）
    bool  m_HasGridReturn = false;
    int   m_HoverRow = -1, m_HoverCol = -1;
    uint32_t m_BookSel = 0;
    float m_Rest = 0.0f;            // カーソルが止まっている時間
    DirectX::SimpleMath::Vector2 m_TooltipAnchor = { 0.0f, 0.0f };

    // 十字の押し続けの連続移動
    int   m_HeldX = 0, m_HeldY = 0;
    float m_RepeatTimer = 0.0f;

    Input m_Test;
    bool  m_HasTest = false;
    bool  m_TestMode = false;   // TEMP-TEST: 一度 TestPress したら、その後もパッド扱い（押していないフレームにマウス扱いへ戻らない）
};
