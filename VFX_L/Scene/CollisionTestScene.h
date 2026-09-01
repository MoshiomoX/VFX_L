// ============================================================
// CollisionTestScene.h
// ?? + ?? + ??? + ??? VFX / ????? + ??????
//
// ????(spells / areas)?????????????????
//   ??????????????????? Items/*.h ???????
// ?????????? PlayerStatsComponent ????
//   ????????????(roguelite ?????????1??????)?
// ?????? Graphics ??????????????
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/FollowCamera.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"

#include "Collider/CollisionSystem.h"
#include "ECS/System/PhysicsSystem.h"
#include "Player/PlayerControlSystem.h"
#include "Player/PlayerStateSystem.h"
#include "ECS/System/WeaponSystem.h"
#include "ECS/System/ProjectileSystem.h"
#include "ECS/System/ProjectileVFXSystem.h"
#include "Graphics/Renderer/ProjectileBillboardRenderer.h"
#include "ECS/System/BackpackAggregateSystem.h"
#include "Item/ExpOrbSystem.h"
#include "UI/LevelUpSystem.h"
#include "ECS/System/RenderSystem.h"
#include "Particle/GPUParticleSystem.h"
#include "VFX_Editor/VFXEffect.h"
#include "UI/UIManager.h"

#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/BackpackUI.h"
#include "UI/LevelUpUI.h"
#include "UI/HUD.h"

#include "SpellID.h"      // ItemID

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
    // ---- ????? ----
    void UpdateScreenSize();
    void UpdateGameplay(float dt);
    void UpdateLevelUpChoice();       // ??????????

    // ---- ImGui ??? ----
    void DrawDebugUI();
    void DrawPlayerPanel();
    void DrawBackpackPanel();
    void DrawWandPanel();
    void DrawStressPanel();

	// ---- UI ----
    void UpdateUI();                  // ?????? UI ???
    void DrawUI();                    // ?????? UI ???
    
    // ---- ?????? ----
    void DrawColliderDebug(Entity e, const Color& color);
    void DrawWandDebug();

    // ---- ?? / ??? ----
    void RebuildPlayerMesh();
    void SpawnEnemy(const Vector3& pos, bool invincible = false);
    void RespawnEnemies();
    void StressSpawnProjectiles(int count);
    int  CountProjectiles() const;
    void RegisterItemVisuals();

private:
    FollowCamera m_Camera;
    Registry     m_Registry;

    // ============================================================
    // Systems
    // ???? UpdateGameplay ?????????
    // ????????????????
    // ============================================================
    CollisionSystem         m_CollisionSystem;
    PhysicsSystem           m_PhysicsSystem;
    PlayerControlSystem     m_PlayerControlSystem;
    PlayerStateSystem       m_PlayerStateSystem;
    WeaponSystem            m_WeaponSystem;
    ProjectileSystem        m_ProjectileSystem;
    ProjectileVFXSystem     m_ProjectileVFXSystem;
    ExpOrbSystem            m_ExpOrbSystem;
    LevelUpSystem           m_LevelUpSystem;
    BackpackAggregateSystem m_BackpackAggregate;
    RenderSystem            m_RenderSystem;

    // --- Particle / VFX / Billboard ---
    GPUParticleSystem           m_ParticleSystem;
    ProjectileBillboardRenderer m_ProjectileRenderer;
    VFXContext                  m_VFXContext;
    std::shared_ptr<Texture>    m_ParticleTexture;
    float m_TotalTime = 0.0f;

    // --- UI ---
    SpriteRenderer m_SpriteRenderer;
    TextRenderer   m_TextRenderer;
    UIManager      m_UI;
    BackpackUI     m_BackpackUI;
    LevelUpUI      m_LevelUpUI;
    HUD            m_HUD;
    //bool           m_BackpackOpen = false;
    bool           m_PauseOnBackpack = true;

    // --- ?????(Graphics ????)---
    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;

    // --- Entities ---
    Entity m_Player = 0;
    std::vector<Entity> m_Terrain;
    std::vector<Entity> m_Enemies;

    // --- ????? ---
    std::shared_ptr<Model> m_EnemyModel;
    std::shared_ptr<Model> m_DummyModel;
    std::shared_ptr<Model> m_StressModel;

    // --- ???? ---
    bool m_ShowWireframe = true;
    bool m_ShowMesh = true;
    bool m_ShowWandDebug = true;
    bool m_ShowBillboard = true;

    // ???????????????????????
    // ??????????????????????????
    bool m_ShowParticle = true;

    // --- ??? ---
    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_LightColor[3] = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    float m_AmbientColor[3] = { 0.3f, 0.3f, 0.3f };

    // ============================================================
    // ????????????????
    //
    // moveSpeed / jumpPower / radius / height ?
    // PlayerStatsComponent ???????????????
    // ??????? +10% ?????????2?????
    //
    // ???????:
    //   color   = ??????(??????)
    //   gravity = ???????(????????????)
    //   spawnPos= ????????????
    // ============================================================
    float m_PlayerColor[3] = { 0.3f, 0.6f, 1.0f };
    float m_Gravity = -20.0f;
    float m_SpawnPos[3] = { 0.0f, 5.0f, 0.0f };

    // ============================================================
    // ?????
    // ============================================================
    int  m_StressCount = 500;
    int  m_StressPending = 0;
    bool m_StressWithModel = false;
    bool m_StressWithCollider = true;

    // ??? ON ?????? emitter ?1??????????
    // ??????(EmitCS / deadList)????????????
    bool   m_StressWithVFX = false;
    ItemID m_StressVFXItem = ItemID::Fireball;

    // ??????????????????????????
    // deadCount ??????????????????????????
    bool  m_StressAutoRefill = false;
    int   m_RefillTarget = 1000;
    int   m_RefillBatch = 100;
    float m_RefillTimer = 0.0f;
    float m_RefillInterval = 0.1f;

    // --- Flush ? CPU ??(??????????????)---
    double m_FlushMs = 0.0;
    double m_FlushMsAvg = 0.0;
    double m_FlushMsPeak = 0.0;

    // --- Emitter ??(Flush ???????????)---
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;

    // ============================================================
    // ???????????
    // ??? slider ?????????????????????
    // ============================================================
    struct StressPreset
    {
        const char* name;
        const char* purpose;
        int   target;
        int   batch;
        bool  autoRefill;
        bool  withVFX;
        bool  withCollider;
        bool  withModel;
    };

    static constexpr StressPreset kStressPresets[] = {
        { "P1 Starve 4000", "pool starvation + heavy waste",
          4000, 100, true,  true,  false, false },
        { "P2 Starve 1024", "same 1024 emitters, no waste",
          1024, 100, true,  true,  false, false },
        { "P3 No VFX 4000", "isolate VFX cost (same 4000 projectiles)",
          4000, 100, true,  false, false, false },
        { "P4 Light 500",   "baseline, everything comfortable",
          500,  50,  true,  true,  false, false },
        { "C1 Collider 500", "collision only, no VFX",
          500,  50,  true,  false, true,  false },
        { "C2 Collider 2000","collision scaling test",
          2000, 100, true,  false, true,  false },
        { "D1 Model 1000",  "3D model per projectile (draw call heavy)",
          1000, 50,  true,  false, false, true  },
    };

    void ApplyStressPreset(const StressPreset& p);
    int  m_LastPresetIndex = -1;
};