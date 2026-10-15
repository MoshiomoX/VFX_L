#include "Core/Game.h"
#include "Graphics/Renderer/Renderer.h"
#include "Scene/CollisionTestScene.h"
#include "Scene/VFXEditorScene.h"
#include "Scene/ProjectileEditorScene.h"
#include "Scene/LevelEditorScene.h"
#include "Scene/SpellLabScene.h"
#include "Scene/TitleScene.h"
#include "Scene/ResultScene.h"
#include "Scene/ShopScene.h"
#include "Scene/RunResult.h"
#include "Core/DevUI.h"
Game::Game()
{
    // 初期化 SceneManager（シーン登録）
}

Game::~Game() = default;

bool Game::Initialize(Renderer* renderer)
{

    m_Renderer = renderer;
    if (!m_Renderer) return false;

	m_SceneManager.RegisterScene<CollisionTestScene>(SceneType::COLLISION_TEST);
	m_SceneManager.RegisterScene<SpellLabScene>(SceneType::SPELL_LAB);
	// エディタ（ImGui で作る）は Demo ビルドには入れない（Core/DevUI.h）
	if constexpr (DevUI::kEnabled)
	{
		m_SceneManager.RegisterScene<VFXEditorScene>(SceneType::VFX_EDITOR);
		m_SceneManager.RegisterScene<ProjectileEditorScene>(SceneType::PROJECTILE_EDITOR);
		m_SceneManager.RegisterScene<LevelEditorScene>(SceneType::LEVEL_EDITOR);
	}
	m_SceneManager.RegisterScene<TitleScene>(SceneType::TITLE);
	m_SceneManager.RegisterScene<ResultScene>(SceneType::RESULT);
	m_SceneManager.RegisterScene<ShopScene>(SceneType::SHOP);

    // 起動はタイトルから。ゲームへ直行したい時は F1（DebugManager のシーン切替）
    m_SceneManager.ChangeScene(SceneType::TITLE);   // TEMP-TEST
    // TEMP-TEST: 粒子テクスチャの自動テスト。VFXL_VFX_AUTOLOAD=<VFXData の json 名> でエフェクト編集へ直行して再生
    // Demo ビルドでは見ない（エディタが無い・自動テストは ImGui の時計を使う物がある）
    char autoloadEnv[128] = {};
    if (!DevUI::kEnabled)
    {
    }
    else if (GetEnvironmentVariableA("VFXL_SHOP_AUTOTEST", autoloadEnv, sizeof(autoloadEnv)) > 0)
    {
        // TEMP-TEST: 商店の自動テスト。第 1 面をクリアした体の引き継ぎを積んで商店へ直行（Scene/ShopScene::AutoTest）
        g_RunCarry.Reset();
        g_RunCarry.stage = 2;
        g_RunCarry.hasPlayer = true;
        g_RunCarry.wallet.gold = 100.0f;
        g_RunCarry.spellbook.Learn(ItemID::HomingBolt, 1);
        g_RunCarry.spellbook.Learn(ItemID::Fireball, 1);
        g_RunCarry.spellbook.Learn(ItemID::Frame3x3, 1);
        g_RunCarry.health.max = 100.0f;
        g_RunCarry.health.current = 100.0f;
        g_RunCarry.mana.max = 100.0f;
        g_RunCarry.mana.current = 100.0f;
        g_RunCarry.mana.regen = 30.0f;
        m_SceneManager.ChangeScene(SceneType::SHOP);
    }
    else if (GetEnvironmentVariableA("VFXL_VFX_AUTOLOAD", autoloadEnv, sizeof(autoloadEnv)) > 0
        || GetEnvironmentVariableA("VFXL_REF_MAGE", autoloadEnv, sizeof(autoloadEnv)) > 0)
        m_SceneManager.ChangeScene(SceneType::VFX_EDITOR);
    else if (GetEnvironmentVariableA("VFXL_MAPEDIT_AUTOTEST", autoloadEnv, sizeof(autoloadEnv)) > 0)
        m_SceneManager.ChangeScene(SceneType::LEVEL_EDITOR);   // TEMP-TEST: 地図エディタ（Debug/AutoTest/AutoTestMapEdit）
    else if (GetEnvironmentVariableA("VFXL_PROJ_AUTOTEST", autoloadEnv, sizeof(autoloadEnv)) > 0)
        m_SceneManager.ChangeScene(SceneType::PROJECTILE_EDITOR);
    else if (GetEnvironmentVariableA("VFXL_SPELL_LAB", autoloadEnv, sizeof(autoloadEnv)) > 0)
        m_SceneManager.ChangeScene(SceneType::SPELL_LAB);   // TEMP-TEST: 魔法の実験場（F7）へ直行
    else if (GetEnvironmentVariableA("VFXL_BATTLE_AUTOTEST", autoloadEnv, sizeof(autoloadEnv)) > 0)
        m_SceneManager.ChangeScene(SceneType::COLLISION_TEST);   // 戦闘の反応エフェクトの自動テスト


	return true;
}

void Game::Update(float dt)
{
    if (!m_IsRunning)
        return;

    m_SceneManager.Update(dt);
}

void Game::Render()
{
    if (!m_IsRunning || !m_Renderer)
        return;
    m_Renderer->Begin();
    m_SceneManager.Render(*m_Renderer);
    m_Renderer->End();
}