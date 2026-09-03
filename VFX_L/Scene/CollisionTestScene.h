// ============================================================
// CollisionTestScene.h
// 衝突 + 物理 + 投射物 + 投射物 VFX / バックパック + プレイヤー
//
// ※杖の内容（spells / areas）はこのシーンが決めるものではない。
//   グリッドの集約結果であり、数値の出どころは Items/*.h に集約されている。
// ※プレイヤーの能力値は PlayerStatsComponent が持つ。
//   シーンはメンバ変数を持たない（roguelite の成長で書き換わるのは
//   常に Entity 側の1ヶ所だけにする）。
// ※デバッグ表示と Graphics まわりだけがシーンの責任。
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
#include "ECS/System/ManaSystem.h"
#include "Enemy/ChaseAISystem.h"
#include "Enemy/SpawnDirector.h"

#include "World/GridWorld.h"

#include "UI/GameUI.h"
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
    // ---- フレーム処理 ----
    void UpdateScreenSize();
    void UpdateGameplay(float dt);



    // ---- ImGui パネル ----
    void DrawDebugUI();
    void DrawPlayerPanel();
    void DrawWandPanel();
    void DrawStressPanel();

    // ---- デバッグ描画 ----
    void DrawColliderDebug(Entity e, const Color& color);
    void DrawWandDebug();

    // ---- 生成 / 再構築 ----
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
    // 実行順は UpdateGameplay の並びがすべて。
    // 宣言順には意味を持たせない。
    // ============================================================
    CollisionSystem         m_CollisionSystem;
    PhysicsSystem           m_PhysicsSystem;
    PlayerControlSystem     m_PlayerControlSystem;
    PlayerStateSystem       m_PlayerStateSystem;
    WeaponSystem            m_WeaponSystem;
    ManaSystem              m_ManaSystem;
    ProjectileSystem        m_ProjectileSystem;
    ProjectileVFXSystem     m_ProjectileVFXSystem;
    ExpOrbSystem            m_ExpOrbSystem;
    LevelUpSystem           m_LevelUpSystem;
    BackpackAggregateSystem m_BackpackAggregate;
    RenderSystem            m_RenderSystem;
	ChaseAISystem 		    m_ChaseAISystem;
    SpawnDirector           m_SpawnDirector;

    // --- Particle / VFX / Billboard ---
    GPUParticleSystem           m_ParticleSystem;
    ProjectileBillboardRenderer m_ProjectileRenderer;
    VFXContext                  m_VFXContext;
    std::shared_ptr<Texture>    m_ParticleTexture;
    float m_TotalTime = 0.0f;

    GameUI m_GameUI;
    GridWorld m_Grid;

    // --- 画面サイズ（Graphics から毎フレーム取る）---
    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;

    // --- Entities ---
    Entity m_Player = 0;
    std::vector<Entity> m_Terrain;
    std::vector<Entity> m_Enemies;

    // --- 使い回すモデル ---
    std::shared_ptr<Model> m_EnemyModel;
    std::shared_ptr<Model> m_DummyModel;
    std::shared_ptr<Model> m_StressModel;

    // --- 表示切替 ---
    bool m_ShowWireframe = true;
    bool m_ShowMesh = true;
    bool m_ShowWandDebug = true;
    bool m_ShowBillboard = true;

    // ※粒子だけを個別に消せるようにしておく。
    //   負荷の出どころが粒子かどうかを切り分けるため。
    bool m_ShowParticle = true;

    // --- 照明 ---
    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_LightColor[3] = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    float m_AmbientColor[3] = { 0.3f, 0.3f, 0.3f };

    // ============================================================
    // プレイヤー調整用（シーン側に残す値）
    //
    // moveSpeed / jumpPower / radius / height は
    // PlayerStatsComponent が持つのでここには置かない。
    // 両方に置くと「移動速度 +10%」の書き先が2ヶ所になる。
    //
    // ここに残すもの:
    //   color    = 見た目だけの値（能力値ではない）
    //   gravity  = シーンの環境値（プレイヤーの能力ではない）
    //   spawnPos = テスト用の復帰位置
    // ============================================================
    float m_PlayerColor[3] = { 0.3f, 0.6f, 1.0f };
    float m_Gravity = -20.0f;
    float m_SpawnPos[3] = { 0.0f, 5.0f, 0.0f };

    // ============================================================
    // 負荷テスト
    // ============================================================
    int  m_StressCount = 500;
    int  m_StressPending = 0;
    bool m_StressWithModel = false;
    bool m_StressWithCollider = true;

    // ※ON にすると投射物1つにつき emitter が1つ積まれる。
    //   粒子側の経路（EmitCS / deadList）に負荷をかけたい時だけ使う。
    bool   m_StressWithVFX = false;
    ItemID m_StressVFXItem = ItemID::Fireball;

    // ※投射物の数を一定に保ち続けて、プールを枯らした状態を維持する。
    //   deadCount ガードが効いているかを確認できる唯一の状態。
    bool  m_StressAutoRefill = false;
    int   m_RefillTarget = 1000;
    int   m_RefillBatch = 100;
    float m_RefillTimer = 0.0f;
    float m_RefillInterval = 0.1f;

    // --- Flush の CPU 時間（GPU を待っていないかの指標）---
    double m_FlushMs = 0.0;
    double m_FlushMsAvg = 0.0;
    double m_FlushMsPeak = 0.0;

    // --- Emitter 統計（Flush でクリアされる前に退避）---
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;

    // ============================================================
    // 負荷テストの既定値セット
    // ※毎回 slider を並べ直すと再現条件がぶれるので表にする。
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

    // --- 敵生成の調整 ---
    int      m_SpawnPointCount = 6;    // 生成点の数
    uint32_t m_TerrainSeed = 1;        // 地形の seed（ImGui から変えて Regenerate）
};