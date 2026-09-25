// ============================================================
// GameUI.h
// ゲーム内 UI の束ね役。
// シーンから呼ぶのは Initialize / Layout / Update / Render /
// DrawDebugUI / Clear / ShouldPauseGame だけ。
//
// 持つもの:
//   SpriteRenderer / TextRenderer（描画資源）
//   UIManager（モーダルの排他。スタック順がそのまま優先度）
//   BackpackUI / LevelUpUI / HUD（各画面）
//
// 持たないもの:
//   BackpackAggregateSystem。あれは gameplay の System なのでシーンに残す。
//   ImGui の Backpack パネルが集約ログを出すために、
//   DrawDebugUI で参照を借りるだけにする。
//
// ※次の拡張:
//   SpellbookUI と、BackpackUI と共有する DragContext をここに足す。
//   「魔法書からグリッドへ跨ぐドラッグ」の状態を
//   2つの UI の上に置くための器がこのクラス。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/UIManager.h"
#include "UI/BackpackUI.h"
#include "UI/LevelUpUI.h"
#include "UI/HUD.h"
#include "UI/DragContext.h"
#include "UI/SpellbookUI.h"
#include "UI/ItemSheetView.h"
#include "UI/PauseMenuUI.h"
struct ID3D11Device;
struct ID3D11DeviceContext;
class Registry;
class BackpackAggregateSystem;

class GameUI
{
public:
    // ※ItemDatabase::Initialize の後に呼ぶこと（LoadIcons が定義を読む）
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
        float screenW, float screenH);
    void Shutdown();

    // 画面サイズが変わった時にシーンから呼ぶ
    void Layout(float screenW, float screenH);

    // 入力処理。割り込みの受け付け → 開閉 → 一番上にだけ入力を渡す
    // ※dt は今は未使用。魔法書の 2D 物理を入れる時に使う
    void Update(Registry& reg, Entity player, float dt);

    // スプライトと文字をまとめて 1 batch で描く（HUD + モーダル）
    void Render(Registry& reg, Entity player);

    // ImGui。UI 内部のデバッグ表示はここに集める。
    // 集約ログの表示と Force Rebuild のために aggregate を借りる
    void DrawDebugUI(Registry& reg, Entity player, BackpackAggregateSystem& aggregate);

    // 三択は必ず止める。グリッドは設定次第
    bool ShouldPauseGame() const;

    // 全部下ろす（プレイヤー消失時など）
    void Clear() { m_Stack.Clear(); }

    // 画面下の操作案内（「[F] Open」など）。nullptr で消す。毎フレームシーンが入れ直す。
    // モーダルが開いている間は出さない（HUD と同じ）
    void SetPrompt(const wchar_t* text) { m_Prompt = text; }

    // 一時停止のメニューで選ばれた物（やり直す / タイトルへ）。取ったら None に戻る。
    // 場面の切替はシーンが行う（GameUI は SceneManager を知らない）
    PauseMenuUI::Action ConsumeMenuAction()
    {
        const auto a = m_MenuAction;
        m_MenuAction = PauseMenuUI::Action::None;
        return a;
    }

    // 死んでからの秒数。0 以上なら「力尽きた」の幕を出す（負で消す）。毎フレームシーンが入れる
    void SetGameOver(float secondsSinceDeath) { m_GameOverTime = secondsSinceDeath; }

    // HUD の経過時間・撃破数と、画面外の目印（箱・精英）。毎フレームシーンが入れ直す
    void SetRunInfo(float runTime, uint32_t kills)
    {
        m_FrameInfo.runTime = runTime;
        m_FrameInfo.kills = kills;
    }
    void SetMarkers(const DirectX::SimpleMath::Matrix& viewProj, std::vector<HUDMarker> markers)
    {
        m_FrameInfo.viewProj = viewProj;
        m_FrameInfo.markers = std::move(markers);
    }

private:
    void UpdateStack(Registry& reg, Entity player, float dt);   // 開閉と入力の振り分け
    void DrawModals(Registry& reg, Entity player);    // スタック順に描く
    void DrawOverlay(Registry& reg, Entity player);   // tooltip（本体の文字より上に出す別の組）

    std::shared_ptr<Texture> m_WhiteTex;              // 無地の白（tooltip の箱・区切り線）
    ItemSheetView::Style     m_TooltipStyle;          // 画面短辺 900px の時の大きさ

    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    UIManager      m_Stack;
    BackpackUI     m_Backpack;
    SpellbookUI    m_Spellbook;
    LevelUpUI      m_LevelUp;
	DragContext    m_Drag;
    HUD            m_HUD;

    bool  m_PauseOnBackpack = true;
    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;

    const wchar_t* m_Prompt = nullptr;   // 画面下の操作案内（SetPrompt）
    HUDFrameInfo   m_FrameInfo;          // SetRunInfo / SetMarkers。wand は Render で入れる

    PauseMenuUI         m_Pause;
    PauseMenuUI::Action m_MenuAction = PauseMenuUI::Action::None;
    float               m_GameOverTime = -1.0f;   // SetGameOver

    void DrawGameOver();
};