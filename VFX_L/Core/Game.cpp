#include "Core/Game.h"
#include "Graphics/Renderer/Renderer.h"
#include "Scene/TestScene.h"
#include "Scene/GPUParticleTestScene.h"
#include "Scene/GYScene.h"
#include "Scene/CollisionTestScene.h"
#include "Scene/VFXEditorScene.h"
Game::Game()
{
    // 初始化 SceneManager（注册场景）
}

Game::~Game() = default;

bool Game::Initialize(Renderer* renderer)
{

    m_Renderer = renderer;
    if (!m_Renderer) return false;

    //m_SceneManager.RegisterScene<GPUParticleTestScene>(SceneType::GPU_PARTICLE_TEST);
    //m_SceneManager.ChangeScene(SceneType::GPU_PARTICLE_TEST);

    //m_SceneManager.RegisterScene<TestScene>(SceneType::TEST);
    //m_SceneManager.ChangeScene(SceneType::TEST);

    // m_SceneManager.RegisterScene<GYScene>(SceneType::GY);
    // m_SceneManager.ChangeScene(SceneType::GY);

	m_SceneManager.RegisterScene<CollisionTestScene>(SceneType::COLLISION_TEST);
	m_SceneManager.RegisterScene<VFXEditorScene>(SceneType::VFX_EDITOR);
    m_SceneManager.ChangeScene(SceneType::COLLISION_TEST);


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