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

private:
    void UpdateStack(Registry& reg, Entity player, float dt);   // 開閉と入力の振り分け
    void DrawModals(Registry& reg, Entity player);    // スタック順に描く

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
};