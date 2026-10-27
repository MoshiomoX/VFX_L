// ============================================================
// GameUI.h
// ゲーム内 UI の束ね役。
// シーンから呼ぶのは Initialize / Layout / Update / Render /
// DrawDebugUI / Clear / ShouldPauseGame だけ。
//
// 持つもの:
//   SpriteRenderer / TextRenderer（描画資源）
//   UIManager（モーダルの排他。スタック順がそのまま優先度）
//   BackpackUI / SpellbookUI / LevelUpUI / HUD / 一時停止 / トレーニング（各画面）
//   DragContext（BackpackUI と SpellbookUI が共有する。「魔法書からグリッドへ跨ぐドラッグ」の状態を
//   2 つの UI の上に置くための器がこのクラス）
//
// 持たないもの:
//   BackpackAggregateSystem。あれは gameplay の System なのでシーンに残す。
//   ImGui の Backpack パネルが集約ログを出すために、
//   DrawDebugUI で参照を借りるだけにする。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/UIManager.h"
#include "UI/BackpackUI.h"
#include "UI/LevelUpUI.h"
#include "UI/HUD.h"
#include "UI/DamageNumbers.h"
#include "UI/DragContext.h"
#include "UI/SpellbookUI.h"
#include "UI/ItemSheetView.h"
#include "UI/PauseMenuUI.h"
#include "UI/BackpackPadControl.h"
#include "UI/TrainingMenuUI.h"
struct ID3D11Device;
struct ID3D11DeviceContext;
class Registry;
class BackpackAggregateSystem;
class LevelUpSystem;

class GameUI
{
public:
    // ※ItemDatabase::Initialize の後に呼ぶこと（LoadIcons が定義を読む）
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
        float screenW, float screenH);
    void Shutdown();

    // 画面サイズが変わった時にシーンから呼ぶ
    void Layout(float screenW, float screenH);

    // 入力処理。割り込みの受け付け → 開閉 → 一番上にだけ入力を渡す。
    // dt = 実時間の秒（ゲームが止まっている間も渡す）。魔法書の 2D 物理・HUD の残像・
    // レベルアップの待ち（ゲームが動いている間だけ）・メニューの猶予と長押しに使う
    void Update(Registry& reg, Entity player, float dt);

    // スプライトと文字をまとめて 1 batch で描く（HUD + モーダル）
    void Render(Registry& reg, Entity player);
    // TEMP-TEST: 動画の書き出しで UI を丸ごと描かない（冒頭・締めの映像。Debug/AutoTest/VideoShot）
    bool hideAll = false;

    // ImGui。UI 内部のデバッグ表示はここに集める。
    // 集約ログの表示と Force Rebuild のために aggregate を借りる
    void DrawDebugUI(Registry& reg, Entity player, BackpackAggregateSystem& aggregate);

    // 三択は必ず止める。グリッドは設定次第
    bool ShouldPauseGame() const;

    // 何か開いているか（グリッド・三択・一時停止）。止めない設定のグリッドも含む。
    // 開いている間はマウスで操作するので、シーンはカーソルを出す
    bool IsModalOpen() const { return !m_Stack.IsEmpty(); }

    // TEMP-TEST: 自動テスト（VFXL_BATTLE_AUTOTEST=ui）。マウスを使わずに画面を開閉し、tooltip を指定の位置に出す
    //   layer: 0 = 全部閉じる / 1 = バックパック / 2 = 一時停止 / 3 = 設定のページ / 4 = 遊び方のページ。itemIndex < 0 で tooltip を消す
    void TestShow(int layer);
    void TestPushBackpack();   // TEMP-TEST: 四択の上にバックパックを重ねる（ui）
    const SpellbookUI& GetSpellbook() const { return m_Spellbook; }   // TEMP-TEST: 木箱の物理の記録
    SpellbookUI& GetSpellbookMutable() { return m_Spellbook; }        // TEMP-TEST: 離した所から落とす確認（TestDropHint）
    BackpackPadControl& TestPad() { return m_Pad; }          // TEMP-TEST: パッド操作（padbag）
    const DragContext& GetDrag() const { return m_Drag; }    // TEMP-TEST
    void TestTooltip(int itemIndex, const DirectX::SimpleMath::Vector2& pos) { m_TestTooltipItem = itemIndex; m_TestTooltipPos = pos; }

    // 全部下ろす（プレイヤー消失時など）。掴んでいる物・パッドの閉じる要求・「レベルアップ！」の待ちの表示も下ろす
    void Clear()
    {
        m_Stack.Clear();
        m_Drag.Reset();
        m_PadClosePending = false;
        m_LevelUpWait = false;
        m_LevelUpWaitAge = -1.0f;
    }

    // バックパックの画面を開いているか（チュートリアルの「開いて置こう」の判定。2026-10-13）
    bool IsBackpackOpen() const { return m_Stack.Top() == UILayer::Backpack; }
    // チュートリアル中：一時停止のメニューに「チュートリアルを飛ばす」が出る（ConsumeMenuAction の SkipTutorial）
    void SetTutorialActive(bool on) { m_Pause.SetTutorialActive(on); }

    // 画面下の操作案内（「[F] Open」など）。nullptr で消す。毎フレームシーンが入れ直す。
    // モーダルが開いている間は出さない（HUD と同じ）
    void SetPrompt(const wchar_t* text) { m_Prompt = text; }

    // 一時停止のメニューで選ばれた物（やり直す / タイトルへ）。取ったら None に戻る。
    // シーンの切替はシーンが行う（GameUI は SceneManager を知らない）
    PauseMenuUI::Action ConsumeMenuAction()
    {
        const auto a = m_MenuAction;
        m_MenuAction = PauseMenuUI::Action::None;
        return a;
    }

    // 死んでからの秒数。0 以上なら「力尽きた」の幕を出す（負で消す）。毎フレームシーンが入れる
    // cleared = true なら「ステージクリア」（Boss を倒した）の幕にする
    void SetGameOver(float secondsSinceDeath, bool cleared = false)
    {
        m_GameOverTime = secondsSinceDeath;
        m_GameOverCleared = cleared;
    }
    // Boss の HP 条（0..1）。負で消す。毎フレームシーンが入れる
    void SetBossBar(float hpRatio) { m_FrameInfo.bossHp = hpRatio; }
    void SetEliteBars(std::vector<HUDEliteBar>&& bars) { m_FrameInfo.elites = std::move(bars); }   // 面の固定エリート（2026-10-25）

    // HUD の経過時間・撃破数と、画面外の目印（箱・エリート）。毎フレームシーンが入れ直す
    // stageTime: 面の制限時間（秒）。0 なら経過時間をそのまま出す
    void SetRunInfo(float runTime, uint32_t kills, float stageTime = 0.0f)
    {
        m_FrameInfo.runTime = runTime;
        m_FrameInfo.kills = kills;
        m_FrameInfo.stageTime = stageTime;
    }
    // 面の番号と名前（撃破数の行の頭に「第 2 面 砂漠」）。stage = 0 なら出さない
    void SetStage(int stage, int total, const wchar_t* name) { m_FrameInfo.stage = stage; m_FrameInfo.stageTotal = total; m_FrameInfo.stageName = name; }   // stage = 何面目、total = Tier
    // 金貨（2026-10-04）。HUD の MP バーの下に出す（< 0 = 出さない）
    void SetGold(int gold) { m_FrameInfo.gold = gold; }
    // 目標の一行（撃破数の下、「目標: 」は HUD が付ける）。nullptr で消す。毎フレームシーンが入れ直す（2026-10-07）
    void SetObjective(const wchar_t* text) { m_FrameInfo.objective = text; }
    DamageNumbers& Damage() { return m_Damage; }   // 場面が Add / Update する（gameplay の dt）
    // 画面の真ん中の案内（2026-10-07。押し寄せ・エリート・最終ウェーブ）。text = null で消す
    void SetAnnounce(const wchar_t* text, float alpha, float age)
    {
        m_FrameInfo.announce = text;
        m_FrameInfo.announceAlpha = alpha;
        m_FrameInfo.announceAge = age;
    }
    // 四択の引き直し（金貨）に使う。無ければ引き直しのボタンを出さない
    void SetLevelUpSystem(LevelUpSystem* sys) { m_LevelUpSystem = sys; }
    // トレーニング（実験場）のメニュー（2026-10-07）。true の間だけ T / パッド RB で開く。
    // メニューの依頼はシーンが毎フレーム取り（SpellLab::Apply）、結果の一行を SetTrainingMessage で返す
    void SetTrainingEnabled(bool on) { m_TrainingEnabled = on; }
    std::vector<TrainingRequest> ConsumeTrainingRequests() { return m_Training.ConsumeRequests(); }
    void SetTrainingMessage(const std::wstring& msg) { if (!msg.empty()) m_Training.SetMessage(msg); }
    TrainingMenuUI& TestTraining() { return m_Training; }   // TEMP-TEST: 自動テスト（training）
    void TestOpenTraining(bool open);                        // TEMP-TEST
    void SetMarkers(const DirectX::SimpleMath::Matrix& viewProj, std::vector<HUDMarker> markers)
    {
        m_FrameInfo.viewProj = viewProj;
        m_FrameInfo.markers = std::move(markers);
    }
    // ミニマップの地形の絵（2026-10-19）。地形を建てた / 建て直した後にシーンが呼ぶ。biome = TerrainGenerator::Biome
    void BakeMinimap(const GridWorld& grid, const MapData::Map& map, int biome, uint32_t seed)
    {
        m_HUD.BakeMinimap(grid, map, biome, seed);
    }

private:
    // 魔法書（画面の縁が壁）の中身が入らない四角 = バックパックの盤面の列（画面の左端から盤面の右 + 少し、上下いっぱい）。
    // 盤面の上に物が積もって見えなくならないように（2026-10-27）
    void SetSpellbookObstacle()
    {
        DirectX::SimpleMath::Vector2 mn, mx;
        m_Backpack.PanelRect(mn, mx);
        const float gap = m_Backpack.GetCellSize() * 0.25f;
        m_Spellbook.SetObstacle({ -10.0f, -10.0f }, { mx.x + gap, m_ScreenH + 10.0f });
    }
    void UpdateStack(Registry& reg, Entity player, float dt);   // 開閉と入力の振り分け
    void DrawModals(Registry& reg, Entity player);    // スタック順に描く
    void DrawOverlay(Registry& reg, Entity player);   // tooltip（本体の文字より上に出す別の組）

    std::shared_ptr<Texture> m_WhiteTex;              // 無地の白（tooltip の箱・区切り線）
    ItemSheetView::Style     m_TooltipStyle;          // 画面短辺 900px の時の大きさ
    int m_TestTooltipItem = -1;                       // TEMP-TEST: TestTooltip
    DirectX::SimpleMath::Vector2 m_TestTooltipPos = { 0.0f, 0.0f };

    // tooltip の中身と折り返しの覚え書き（2026-10-25）。以前は指している間、毎フレーム DescribePlaced
    // （影響の組み合わせを全部回す）と 1 字ずつの折り返しを、測る段・描く段で 2 回やり直していた。
    // 指している物・バックパックの番号・形の番号・魔法威力が同じ間は中身を、幅と文字の大きさも同じなら折り返しも使い回す
    struct TooltipCache
    {
        bool     valid = false;
        bool     placed = false;           // true = バックパックに置いてある物（DescribePlaced）/ false = アイテムそのもの（Describe）
        int      index = -1;               // placed の時の items の index
        ItemID   id = ItemID::Unknown;     // placed でない時
        uint32_t bpRevision = 0;
        uint32_t shapeRevision = 0;
        float    spellPower = 1.0f;
        ItemInfo::Sheet sheet;

        bool     layoutValid = false;
        float    layoutWidth = 0.0f, layoutBody = 0.0f, layoutSmall = 0.0f, layoutPad = 0.0f;
        ItemSheetView::BodyLayout layout;
    };
    TooltipCache m_TooltipCache;

    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    TextRenderer   m_DamageText;   // ダメージの数字専用（Res::Fnt::Damage。2026-10-18）。HUD の文字の後に描く
    UIManager      m_Stack;
    BackpackUI     m_Backpack;
    SpellbookUI    m_Spellbook;
    LevelUpUI      m_LevelUp;
	DragContext    m_Drag;
    HUD            m_HUD;
    DamageNumbers  m_Damage;                   // ダメージの数字（HUD と同じ画面座標で描く。2026-10-17）
    BackpackPadControl m_Pad;                  // バックパック画面のパッド操作
    bool           m_PadClosePending = false;  // B で閉じる要求（次のフレームで閉じる）
    bool           m_LevelUpWait = false;      // レベルアップ直後、四択を出すまでの間（LevelComponent::choiceDelay）
    float          m_LevelUpWaitAge = -1.0f;   // その経過秒（案内の出方）

    bool  m_PauseOnBackpack = true;
    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;

    const wchar_t* m_Prompt = nullptr;   // 画面下の操作案内（SetPrompt）
    HUDFrameInfo   m_FrameInfo;          // SetRunInfo / SetMarkers。wand は Render で入れる
    LevelUpSystem* m_LevelUpSystem = nullptr;   // SetLevelUpSystem（引き直し）

    TrainingMenuUI      m_Training;          // トレーニングのメニュー（実験場だけ）
    bool                m_TrainingEnabled = false;
    PauseMenuUI         m_Pause;
    PauseMenuUI::Action m_MenuAction = PauseMenuUI::Action::None;
    float               m_GameOverTime = -1.0f;   // SetGameOver
    bool                m_GameOverCleared = false;

    void DrawGameOver();
};