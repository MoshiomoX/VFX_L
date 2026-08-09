// ============================================================
// CollisionTestScene.h
// 戦闘 + 物理 + カメラ + 投射物 VFX / ビルボード + バックパック
//
// ★杖の中身（spells / areas）はバックパックの集約結果のみで決まる。
//   シーン側で数値を書かない。魔法の定義は Items/*.h が唯一の出処。
// ★画面サイズは Graphics から取得し、変化に追従する。
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
#include "BackpackAggregateSystem.h"
#include "RenderSystem.h"

#include "GPUParticleSystem.h"
#include "VFXEffect.h"

#include "SpriteRenderer.h"
#include "BackpackUI.h"

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
    void UpdateScreenSize();          // 画面サイズの変化に追従する
    void UpdateGameplay(float dt);
    void DrawDebugUI();
    void DrawBackpackPanel();
    void DrawWandPanel();
    void DrawStressPanel();           // ★負荷テスト（粒子発射経路の検証を含む）
    void DrawColliderDebug(Entity e, const Color& color);
    void DrawWandDebug();
    void RebuildPlayerMesh();
    void SpawnEnemy(const Vector3& pos, bool invincible = false);
    void RespawnEnemies();
    void StressSpawnProjectiles(int count);
    int  CountProjectiles() const;    // ★自動補充の判定用
    void RegisterItemVisuals();

private:
    FollowCamera m_Camera;
    Registry     m_Registry;

    // --- Systems ---
    CollisionSystem         m_CollisionSystem;
    PhysicsSystem           m_PhysicsSystem;
    PlayerControlSystem     m_PlayerControlSystem;
    WeaponSystem            m_WeaponSystem;
    ProjectileSystem        m_ProjectileSystem;
    ProjectileVFXSystem     m_ProjectileVFXSystem;
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
    BackpackUI     m_BackpackUI;
    bool           m_BackpackOpen = false;
    bool           m_PauseOnBackpack = true;

    // --- 画面サイズ（Graphics から取得）---
    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;

    // --- Entities ---
    Entity m_Player = 0;
    std::vector<Entity> m_Terrain;
    std::vector<Entity> m_Enemies;

    // --- 共有モデル ---
    std::shared_ptr<Model> m_EnemyModel;
    std::shared_ptr<Model> m_DummyModel;
    std::shared_ptr<Model> m_StressModel;

    // --- 表示切替 ---
    bool m_ShowWireframe = true;
    bool m_ShowMesh = true;
    bool m_ShowWandDebug = true;
    bool m_ShowBillboard = true;

    // --- ライト ---
    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_LightColor[3] = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    float m_AmbientColor[3] = { 0.3f, 0.3f, 0.3f };

    // --- プレイヤー ---
    float m_PlayerColor[3] = { 0.3f, 0.6f, 1.0f };
    float m_PlayerRadius = 0.4f;
    float m_PlayerHeight = 1.0f;
    float m_MoveSpeed = 5.0f;
    float m_JumpPower = 8.0f;
    float m_Gravity = -20.0f;
    float m_SpawnPos[3] = { 0.0f, 5.0f, 0.0f };

    // --- 負荷テスト ---
    int  m_StressCount = 500;
    int  m_StressPending = 0;
    bool m_StressWithModel = false;
    bool m_StressWithCollider = true;

    // ★VFX を付けるか。
    //   これを ON にしない限り emitter が 1 つも生まれないので、
    //   粒子発射経路（EmitCS / deadList）の負荷試験にはならない。
    bool   m_StressWithVFX = false;
    ItemID m_StressVFXItem = ItemID::Fireball;

    // ★自動補充：投射物数を目標値に保ち、粒子プールを枯渇させ続ける。
    //   deadCount 護欄が効いているかは「枯渇状態」でしか検証できない。
    bool  m_StressAutoRefill = false;
    int   m_RefillTarget = 1000;
    int   m_RefillBatch = 100;
    float m_RefillTimer = 0.0f;
    float m_RefillInterval = 0.1f;

    // --- Flush の CPU 時間（①＝毎フレーム回読廃止の検証指標）---
    double m_FlushMs = 0.0;
    double m_FlushMsAvg = 0.0;
    double m_FlushMsPeak = 0.0;

    // --- Emitter 統計（Flush でクリアされる前に退避）---
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;
};