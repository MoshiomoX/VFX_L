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
#include "UI/ItemSheetView.h"
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SpriteRenderer;
class TextRenderer;
class Texture;
struct LevelComponent;

class LevelUpUI
{
public:
    void Initialize(std::shared_ptr<Texture> blockTex);
    void LoadIcons();
    void Layout(float screenW, float screenH);

    // 選ばれた ID を返す。まだ選ばれていなければ false。
    // dt = 実時間の秒（出た直後の猶予を減らす。ゲームが止まっている間も渡す）
    bool HandleInput(const LevelComponent& lv, ItemID& outPicked, float dt = 0.016f);

    // 四択の層を積んだ時に GameUI が呼ぶ（出る時の動き・カーソル・猶予を毎回やり直す）。
    // 以前は Draw が候補 0 枚を見た時にしか戻さず、選んだ同じフレームに層が下りるので、動きは最初の 1 回しか出なかった
    void OnOpen() { m_Shown = false; m_WasChoosing = false; }

    // text は能力値のカード（形が無い物）の文字に使う。SpriteRenderer の後に描かれる
    void Draw(SpriteRenderer& sprite, TextRenderer& text, const LevelComponent& lv);

    // ---- レイアウト（すべて比率）----
    float cardWidthRatio = 0.30f;   // カード幅（画面短辺基準）。名前・説明・能力値が入る幅
    float cardAspect = 1.50f;   // 高さ / 幅
    float cardGapRatio = 0.035f;  // カード間の隙間
    int   fitCards = 4;           // この枚数が横に収まるようにカード幅を抑える（LevelUpSystem::choiceCount と合わせる）

    // カードの文字（幅 270px の時の大きさ。実際はカード幅に比例させる）
    ItemSheetView::Style textStyle;
    float centerY = 0.50f;   // 画面高さに対する中心位置

    // 戦闘の UI はシーンの HDR バッファに描かれ、トーンマップ + ガンマを通る（線形の値）。
    // 画面で 72% 暗くしたいなら 1 - 0.28^2.2 ≒ 0.94
    DirectX::SimpleMath::Vector4 dimColor = { 0.0f, 0.0f, 0.0f, 0.94f };
    // 地は不透明に近く（少し透けると明るい草が混ざって灰色になる）。画面で #0E0C16 くらい
    DirectX::SimpleMath::Vector4 cardColor = { 0.0025f, 0.0020f, 0.0045f, 0.985f };
    DirectX::SimpleMath::Vector4 hoverColor = { 0.0060f, 0.0048f, 0.0100f, 0.985f };

    // ---- 幻想 UI の飾り（UIDeco）----
    float headingScale = 1.10f;   // 見出し「レベルアップ」（カード幅 270px の時）
    DirectX::SimpleMath::Vector4 headingColor = { 0.96f, 0.92f, 0.84f, 1.0f };   // 名前・見出しの暖かい白
    DirectX::SimpleMath::Vector4 iconColor = { 0.97f, 0.94f, 0.87f, 1.0f };      // 白いシルエットに掛ける色
    float cornerRatio = 0.26f;    // 四隅の組紐の大きさ（カード幅に対して）
    float circleSpin = 0.12f;     // 魔法陣の回転（rad / 秒、選択中は倍）

    // ---- 稀有度の縁（2026-10-28。ユーザー：面の色は魔法の種類のまま、報酬の時にカードへ縁を付けて稀有度を見せる）----
    // カードの外側 rarityGap px に太さ rarityWidth の枠（カード幅 270px の時）。レア・レジェンドは外へ光をにじませ、
    // レジェンドは脈打つ。枠の上の真ん中に稀有度の名前の札。色は線形（UI は HDR に描かれるので 1 を超えると bloom で光る）
    float rarityGap = 4.0f;
    float rarityWidth = 3.0f;
    float rarityGlow = 0.45f;          // にじむ光の濃さ（一番内側の層）
    float rarityTextScale = 0.36f;
    DirectX::SimpleMath::Vector4 rarityColor[3] = {
        { 0.16f, 0.16f, 0.19f, 1.0f },    // ノーマル：暗い灰（光らない。0.36 だと銀色に見えて珍しい物に見えた）
        { 0.20f, 0.52f, 1.60f, 1.0f },    // レア：青（少し bloom）
        { 1.90f, 1.05f, 0.18f, 1.0f },    // レジェンド：金（bloom）
    };
    DirectX::SimpleMath::Vector4 rarityTextColor[3] = {
        { 0.70f, 0.70f, 0.74f, 1.0f },
        { 0.50f, 0.78f, 1.00f, 1.0f },
        { 1.00f, 0.82f, 0.36f, 1.0f },
    };

    // 選択中の index（キーボード / パッド用）。カードの枚数と同じ値 = 引き直しのボタン（2026-10-28）
    int GetCursor() const { return m_Cursor; }

    // カード 1 枚（種別 → 名前 → 魔法陣の中のアイコン + 形 → 説明）。pos = 左上。
    // レベルアップの四択と商店（ShopScene）で共用（2026-10-15）。大きさは Layout で決めた CardSize
    void DrawCard(SpriteRenderer& sprite, TextRenderer& text, ItemID id,
        const DirectX::SimpleMath::Vector2& pos, bool selected) const;
    DirectX::SimpleMath::Vector2 CardSize() const;
    bool showRarity = true;   // 稀有度の縁（DrawCard の中で描く）

    // ---- 金貨での引き直し（2026-10-04）----
    // カードの下のボタン（R / パッド X / クリック / 下キー・十字・スティックの下で選んで決定）。
    // GameUI が毎フレーム値段と所持を渡す（cost < 0 なら出さない）
    void SetReroll(int cost, int gold) { m_RerollCost = cost; m_Gold = gold; }
    // 押された（足りるかは見ない。GameUI が LevelUpSystem::Reroll を呼び、足りなければ断りの音）
    bool ConsumeRerollRequest() { const bool r = m_RerollRequested; m_RerollRequested = false; return r; }
    float rerollWidthRatio = 1.60f;   // ボタンの幅（カード幅に対して）
    float rerollTextScale = 0.40f;    // カード幅 270px の時

    // 左スティックでカードを選ぶ（2026-10-28）
    float stickPress = 0.5f;            // これより倒したら 1 枚
    float stickRelease = 0.3f;          // 出た時に倒していたら、これより戻すまで見ない
    float stickRepeatDelay = 0.40f;     // 倒し続けて連続で動き始めるまで（秒）
    float stickRepeatInterval = 0.18f;  // 連続で動く間隔
    // TEMP-TEST: 次の HandleInput 1 回分の左スティック（自動テスト padbag）
    void TestStick(const DirectX::SimpleMath::Vector2& s) { m_TestStick = s; m_HasTestStick = true; }
    // TEMP-TEST: 次の HandleInput 1 回分の「決定」（パッド A / Enter と同じ）
    void TestConfirm() { m_TestConfirm = true; }

    // 区切り線などに使う無地の白（無ければブロックのテクスチャで代用）
    void SetWhiteTexture(std::shared_ptr<Texture> tex) { m_WhiteTex = tex; }

private:
    DirectX::SimpleMath::Vector2 CardPosition(int index, int total) const;
    // 引き直しのボタン（カードの列の下の中央）の左上と大きさ
    void RerollRect(int total, DirectX::SimpleMath::Vector2& pos, DirectX::SimpleMath::Vector2& size) const;
    std::shared_ptr<Texture> GetIcon(ItemID id) const;
    void DrawShapePreview(SpriteRenderer& sprite, const ItemCommon& c,
        const DirectX::SimpleMath::Vector2& areaPos,
        const DirectX::SimpleMath::Vector2& areaSize) const;

    std::shared_ptr<Texture> m_BlockTex;
    std::shared_ptr<Texture> m_WhiteTex;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;

    DirectX::SimpleMath::Vector2 m_ScreenSize = { 1600.0f, 900.0f };
    float m_CardW = 300.0f;
    float m_CardH = 435.0f;
    float m_CardGap = 60.0f;

    int m_Cursor = 0;      // キーボード操作用のカーソル（= カードの枚数ならボタン）
    int m_LastCard = 0;    // ボタンへ下りる前のカード（上・左右で戻る先）
    int m_Hover = -1;      // マウスが乗っているカード
    DirectX::SimpleMath::Vector2 m_LastMouse = { 0.0f, 0.0f };   // 前のフレームのマウス（動いた時だけカーソルを合わせる）

    // 表示された直後に押しっぱなしの入力で確定してしまうのを防ぐ猶予
    float m_InputDelay = 0.0f;
    bool  m_WasChoosing = false;

    // 左スティックでカードを選ぶ（2026-10-28）。出てから一度戻すまでは見ない（m_StickArmed）
    bool  m_StickArmed = false;
    int   m_StickHeld = 0;      // 今倒している向き（±1 = 左右、+2 = 下、-2 = 上、0 = 戻っている）
    float m_StickTimer = 0.0f;  // 次の連続移動まで
    DirectX::SimpleMath::Vector2 m_TestStick = { 0.0f, 0.0f };   // TEMP-TEST: TestStick
    bool  m_HasTestStick = false;
    bool  m_TestConfirm = false;   // TEMP-TEST: TestConfirm

    // 出た瞬間の動き（2026-10-15）：暗幕が濃くなり、カードが下から浮き上がる（実時間 UIDeco::Clock）
    mutable bool  m_Shown = false;
    mutable float m_ShownAt = 0.0f;
    float appearTime = 0.35f;

    int   m_RerollCost = -1;        // SetReroll（< 0 = ボタンを出さない）
    int   m_Gold = 0;
    bool  m_RerollRequested = false;
    bool  m_RerollHover = false;
};