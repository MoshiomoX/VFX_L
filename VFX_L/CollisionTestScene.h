// ============================================================
// CollisionTestScene.h
// 戦闘 + 物理 + カメラ + 投射物 VFX / ビルボードの統合テストシーン
// パーティクルは Submit → Flush の2段階方式（複数 VFX 共存対応）。
// 投射物の見た目は ビルボード芯 + VFX。3D モデルも併用可（既定では未登録）。
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
#include "ProjectileVFXSystem.h"
#include "ProjectileBillboardRenderer.h"
#include "RenderSystem.h"

#include "GPUParticleSystem.h"
#include "VFXEffect.h"

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
    void DrawWandDebug();
    void RebuildPlayerMesh();
    void SpawnEnemy(const Vector3& pos, bool invincible = false);
    void RespawnEnemies();
    void StressSpawnProjectiles(int count);

private:
    FollowCamera m_Camera;
    Registry     m_Registry;

    // --- Systems ---
    CollisionSystem      m_CollisionSystem;
    PhysicsSystem        m_PhysicsSystem;
    PlayerControlSystem  m_PlayerControlSystem;
    WeaponSystem         m_WeaponSystem;
    ProjectileSystem     m_ProjectileSystem;
    ProjectileVFXSystem  m_ProjectileVFXSystem;
    RenderSystem         m_RenderSystem;

    // --- Particle / VFX / Billboard ---
    GPUParticleSystem           m_ParticleSystem;
    ProjectileBillboardRenderer m_ProjectileRenderer;
    VFXContext                  m_VFXContext;
    std::shared_ptr<Texture>    m_ParticleTexture;
    std::shared_ptr<Texture>    m_ProjectileCore;
    float m_TotalTime = 0.0f;

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
    bool m_ShowBillboard = true;   // 投射物の芯を描くか

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

    // --- 投射物の見た目（ImGui 調整用、変更時に再登録）---
    float m_FireSize = 0.9f;
    float m_FireColor[3] = { 1.0f, 0.65f, 0.25f };
    float m_FireStretch = 0.0f;
    float m_BoltSize = 0.6f;
    float m_BoltColor[3] = { 0.5f, 0.85f, 1.0f };
    float m_BoltStretch = 1.5f;

    // --- 負荷テスト ---
    int  m_StressCount = 500;
    int  m_StressPending = 0;
    bool m_StressWithModel = false;   // 既定はビルボードで測る
    bool m_StressWithCollider = true;

    // --- Emitter 統計（Flush でクリアされる前に退避）---
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;
};