// ============================================================
// CollisionTestScene.h
// 戦闘 + 物理 + カメラ + 投射物 VFX / ビルボード + 呪文グリッド
//
// 杖の中身（spells / areas）はグリッドの集約結果のみで決まる。
//   シーン側で数値を書かない。魔法の定義は Items/*.h が唯一の出処。
// プレイヤーの能力値は PlayerStatsComponent が持つ。
//   シーンはコピーを持たない（roguelite の成長を書く場所を1つにするため）。
// 画面サイズは Graphics から取得し、変化に追従する。
// ============================================================
#pragma once
#include "SceneBase.h"
#include "FollowCamera.h"
#include "Registry.h"
#include "Entity.h"

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PlayerControlSystem.h"
#include "PlayerStateSystem.h"
#include "WeaponSystem.h"
#include "ProjectileSystem.h"
#include "ProjectileVFXSystem.h"
#include "ProjectileBillboardRenderer.h"
#include "BackpackAggregateSystem.h"
#include "ExpOrbSystem.h"
#include "LevelUpSystem.h"
#include "RenderSystem.h"

#include "GPUParticleSystem.h"
#include "VFXEffect.h"

#include "SpriteRenderer.h"
#include "BackpackUI.h"
#include "LevelUpUI.h"

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
    // ---- 毎フレーム ----
    void UpdateScreenSize();
    void UpdateGameplay(float dt);
    void UpdateLevelUpChoice();       // 選択待ちの間だけ回す

    // ---- ImGui パネル ----
    void DrawDebugUI();
    void DrawPlayerPanel();
    void DrawBackpackPanel();
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
    // 実行順は UpdateGameplay のコメントを参照。
    // ここの宣言順は実行順とは無関係。
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
    BackpackUI     m_BackpackUI;
    LevelUpUI      m_LevelUpUI;
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

    // 粒子を消して投射物の芯だけを見えるようにする。
    // 加算合成の光の中では実体の位置が分からなくなるため。
    bool m_ShowParticle = true;

    // --- ライト ---
    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_LightColor[3] = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    float m_AmbientColor[3] = { 0.3f, 0.3f, 0.3f };

    // ============================================================
    // プレイヤー関連でシーンが持つもの
    //
    // moveSpeed / jumpPower / radius / height は
    // PlayerStatsComponent へ移動した。ここには置かない。
    // 置くと「成長で +10% 速度」を書く場所が2つになる。
    //
    // 残しているのは：
    //   color   = 見た目の選択（シーンの演出）
    //   gravity = 場のパラメータ（プレイヤーの能力ではない）
    //   spawnPos= このシーン固有の初期位置
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

    // これを ON にしない限り emitter が1つも生まれないので、
    // 粒子発射経路（EmitCS / deadList）の負荷試験にはならない。
    bool   m_StressWithVFX = false;
    ItemID m_StressVFXItem = ItemID::Fireball;

    // 投射物数を目標値に保ち、粒子プールを枯渇させ続ける。
    // deadCount のガードが効いているかは枯渇状態でしか検証できない。
    bool  m_StressAutoRefill = false;
    int   m_RefillTarget = 1000;
    int   m_RefillBatch = 100;
    float m_RefillTimer = 0.0f;
    float m_RefillInterval = 0.1f;

    // --- Flush の CPU 時間（毎フレーム回読廃止の検証指標）---
    double m_FlushMs = 0.0;
    double m_FlushMsAvg = 0.0;
    double m_FlushMsPeak = 0.0;

    // --- Emitter 統計（Flush でクリアされる前に退避）---
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;

    // ============================================================
    // 負荷テストのプリセット
    // 手動で slider を動かすと毎回条件がズレて比較にならない。
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