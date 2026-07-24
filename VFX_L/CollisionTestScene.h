// ============================================================
// CollisionTestScene.h
// 戦闘 + 物理 + カメラ追従の統合テストシーン
// 杖は出力源（spells）ごとに独立発射。分裂・二重釈放を可視化付きで確認する。
// ============================================================
#pragma once
#include "SceneBase.h"
#include "FollowCamera.h"
#include "Registry.h"
#include "Entity.h"

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PlayerControlSystem.h"
#include "WeaponSystem.h"
#include "ProjectileSystem.h"
#include "RenderSystem.h"

#include <memory>
#include <vector>

class Model;

class CollisionTestScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    void DrawDebugUI();
    void DrawColliderDebug(Entity e, const Color& color);
    void DrawWandDebug();                 // 施法の可視化
    void RebuildPlayerMesh();
   
    void RespawnEnemies();
    void SpawnEnemy(const Vector3& pos, bool invincible = false);
    void StressSpawnProjectiles(int count);   // 負荷テスト用：投射物を一気に生成

private:
    FollowCamera m_Camera;
    Registry     m_Registry;

    // --- Systems ---
    CollisionSystem     m_CollisionSystem;
    PhysicsSystem       m_PhysicsSystem;
    PlayerControlSystem m_PlayerControlSystem;
    WeaponSystem        m_WeaponSystem;
    ProjectileSystem    m_ProjectileSystem;
    RenderSystem        m_RenderSystem;
    // --- Entities ---
    Entity m_Player = 0;
    std::vector<Entity> m_Terrain;
    std::vector<Entity> m_Enemies;

    // --- 共有モデル ---
    std::shared_ptr<Model> m_EnemyModel;
    std::shared_ptr<Model> m_DummyModel;
    std::shared_ptr<Model> m_StressModel;
    // --- ImGui 調整用 ---
    bool m_ShowWireframe = true;
    bool m_ShowMesh = true;
    bool m_ShowWandDebug = true;

    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_LightColor[3] = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    float m_AmbientColor[3] = { 0.3f, 0.3f, 0.3f };

    float m_PlayerColor[3] = { 0.3f, 0.6f, 1.0f };
    float m_PlayerRadius = 0.4f;
    float m_PlayerHeight = 1.0f;
    float m_MoveSpeed = 5.0f;
    float m_JumpPower = 8.0f;
    float m_Gravity = -20.0f;

    float m_SpawnPos[3] = { 0.0f, 5.0f, 0.0f };
    
    
    int  m_StressCount = 500;
};