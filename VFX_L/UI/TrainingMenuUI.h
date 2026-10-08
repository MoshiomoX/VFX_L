// ============================================================
// TrainingMenuUI.h
// トレーニング（魔法の実験場）のメニュー（2026-10-07 ユーザー：的・雑魚の群れ・Boss を出す、
// 魔法を木箱へ入れる、を ImGui ではなくゲームの UI で）。T / パッド RB で開閉、開いている間はゲームが止まる。
//   行 = 的を置く（正面 3 / 周り 12 / エリート）、的の HP、群れの種類、群れの数、敵が動く、
//        敵を出す（群れ / ボス / 全部消す）、木箱に入れる（魔法・ルーンの絵の並び）、木箱（全部入れる / 空にする）、戻る
//   キー   : W S / 上下で行、A D / 左右で値・ボタン・絵を選ぶ、Enter / Space で決定、Esc / T で閉じる
//   パッド : 十字、A で決定、B / RB で閉じる
//   マウス : 動かした時だけ選ぶ。クリックで決定（値の行は < > の上）
// やる事は「依頼」を積むだけ（ConsumeRequests）。実際に湧かせる・箱へ入れるのはシーン → SpellLab::Apply。
// 画面が止まっている間に使うので dt は取らない（SettingsMenuUI と同じ）
// ============================================================
#pragma once
#include "SpellID.h"
#include <SimpleMath.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SpriteRenderer;
class TextRenderer;
class Texture;
struct SpellbookComponent;

// メニューからシーンへの依頼
struct TrainingRequest
{
    enum class Kind { Targets, Swarm, Boss, KillAll, AddItem, AddAll, ClearChest };
    Kind   kind = Kind::Targets;
    int    pattern = 0;            // Targets：0 正面 3 体 / 1 周り 12 体 / 2 エリート
    float  targetHp = 1.0e6f;      // Targets
    int    swarmKind = 0;          // Swarm：0 雑魚 / 1 自爆兵 / 2 スプリッター / 3 重装兵 / 4 幽霊 / 5 混合
    int    count = 30;             // Swarm
    bool   moving = true;          // Swarm / Boss（false = その場に止まったまま）
    ItemID item = ItemID::Unknown; // AddItem
};

class TrainingMenuUI
{
public:
    TrainingMenuUI();

    void Layout(float screenW, float screenH);
    void Open();
    // 開いている間、毎フレーム。閉じる（戻る / B / Esc）なら true
    bool HandleInput();
    // book = 木箱の中身（絵の右下に個数を出す。null なら出さない）
    void Draw(SpriteRenderer& sprite, TextRenderer& text, const std::shared_ptr<Texture>& white,
        const SpellbookComponent* book);

    void SetIconLookup(std::function<std::shared_ptr<Texture>(ItemID)> f) { m_IconLookup = std::move(f); }
    std::vector<TrainingRequest> ConsumeRequests() { auto r = std::move(m_Requests); m_Requests.clear(); return r; }
    // 結果の一行（シーンが入れる。しばらくすると消える）
    void SetMessage(const std::wstring& msg) { m_Message = msg; m_MessageTime = 2.5f; }

    float panelWidthRatio = 1.30f;   // 画面短辺に対する比率（画面幅の 95% までに収める）
    float rowHeightRatio = 0.058f;
    float itemRowRatio = 0.085f;     // 絵の行の高さ
    float rowGapRatio = 0.010f;
    DirectX::SimpleMath::Vector4 dimColor = { 0.0f, 0.0f, 0.0f, 0.80f };
    DirectX::SimpleMath::Vector4 panelColor = { 0.0025f, 0.0020f, 0.0045f, 0.985f };

    // TEMP-TEST: 自動テスト（VFXL_BATTLE_AUTOTEST=training）。row = RowId の番号の最初の行、col = その中の部品
    void TestActivate(int rowId, int col);

    // 選べる値（描画と依頼で共用）
    static constexpr int kHpChoices = 5;
    static constexpr int kKindChoices = 6;
    static constexpr int kCountChoices = 4;
    static const float kTargetHp[kHpChoices];
    static const wchar_t* const kSwarmKinds[kKindChoices];
    static const int kSwarmCounts[kCountChoices];

private:
    // 箱の中の割合（画面短辺 × 倍率に対して）
    static constexpr float kPadRatio = 0.035f;
    static constexpr float kTitleRatio = 0.085f;
    static constexpr float kFootRatio = 0.11f;    // 下の 2 行（選んでいる物の説明 / 結果、操作の案内）
    static constexpr float kLabelRatio = 0.22f;   // 行の幅のうち左の名前が占める割合
    static constexpr int   kItemsPerRow = 9;

    void Build();             // 行を組む（魔法の一覧は ItemDatabase から。初期化の後で呼ぶ）
    enum class RowKind { Buttons, Choice, Toggle, Items, Back };
    enum class RowId { Targets, TargetHp, SwarmKind, SwarmCount, Moving, Spawn, Items, Chest, Back };
    struct Row
    {
        std::wstring label;
        RowKind kind = RowKind::Buttons;
        RowId id = RowId::Back;
        std::vector<std::wstring> buttons;   // Buttons
        std::vector<ItemID> items;           // Items（1 行分）
    };

    int  ColCount(const Row& r) const;
    void Step(int dir);       // 値の行を 1 段（Choice / Toggle）
    void Activate();          // Enter / A / クリック
    std::wstring ValueText(const Row& r) const;
    float RowHeight(int i) const;
    DirectX::SimpleMath::Vector2 RowPos(int i) const;
    // 右側の操作部の矩形と、その中の col 番目の部品（ボタン・絵）の矩形
    void ControlRect(int i, DirectX::SimpleMath::Vector2& pos, DirectX::SimpleMath::Vector2& size) const;
    void CellRect(int i, int col, DirectX::SimpleMath::Vector2& pos, DirectX::SimpleMath::Vector2& size) const;
    int  HitTest(const DirectX::SimpleMath::Vector2& p, int& col) const;   // 行（-1 = 無し）と部品
    ItemID FocusedItem() const;

    std::vector<Row> m_Rows;
    int   m_Row = 0;
    int   m_Col = 0;
    float m_InputDelay = 0.0f;
    float m_RepeatDelay = 0.0f;
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };

    // 選んでいる値
    int  m_HpIndex = 4;       // kTargetHp
    int  m_KindIndex = 0;     // kSwarmKinds
    int  m_CountIndex = 1;    // kSwarmCounts
    bool m_Moving = true;

    std::vector<TrainingRequest> m_Requests;
    std::wstring m_Message;
    float m_MessageTime = 0.0f;
    std::function<std::shared_ptr<Texture>(ItemID)> m_IconLookup;

    DirectX::SimpleMath::Vector2 m_Screen = { 1600.0f, 900.0f };
    DirectX::SimpleMath::Vector2 m_PanelPos = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_PanelSize = { 0.0f, 0.0f };
    float m_Short = 900.0f;
    float m_Scale = 1.0f;
    float m_RowsTop = 0.0f;
    float m_RowH = 0.0f;
    float m_ItemH = 0.0f;
    float m_Gap = 0.0f;
};
