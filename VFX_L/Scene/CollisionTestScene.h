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
// ※雑魚は GPU（SwarmSystem）の中にしか居ない。
//   CPU の Registry に居る敵は EliteTag（精英・計測用の的）だけ。
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
#include "Swarm/AreaVFXPlayer.h"
#include "ECS/System/MeshVFXSystem.h"
#include "Graphics/Renderer/ProjectileBillboardRenderer.h"
#include "ECS/System/BackpackAggregateSystem.h"
#include "UI/LevelUpSystem.h"
#include "ECS/System/RenderSystem.h"
#include "Particle/GPUParticleSystem.h"
#include "VFX_Editor/VFXEffect.h"
#include "ECS/System/ManaSystem.h"
#include "Enemy/SpawnDirector.h"

#include "World/GridWorld.h"
#include "Swarm/SwarmSystem.h"

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
    void UpdateMeshEmitTest(float dt);   // Mesh 発射の動作確認（仮設）

    // ---- ImGui パネル ----
    void DrawDebugUI();
    void DrawPlayerPanel();
    void DrawWandPanel();
    void DrawStressPanel();
    void DrawBloomPanel();   // 後処理 bloom の調整（Graphics 側の BloomParams を直接触る）

    // ---- デバッグ描画 ----
    void DrawColliderDebug(Entity e, const Color& color);
    void DrawWandDebug();

    // ---- 生成 / 再構築 ----
    void RebuildPlayerMesh();
    void SpawnElite(const Vector3& pos);   // CPU 側の的（無敵・動かない）
    void RespawnElites();
    void StressSpawnProjectiles(int count);
    int  CountProjectiles() const;
    void EndRun();                         // 戦績を書いてリザルトへ
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
    AreaVFXPlayer           m_AreaVFX;             // CPU から出した範囲攻撃の見た目
    int                     m_AreaTestProfile = 1; // Swarm パネルの Area Test 用
    MeshVFXSystem           m_MeshVFXSystem;       // モデル表面からの粒子（燃焼消滅など）
    float                   m_BurnDuration = 1.5f; // 燃焼消滅の秒数（ImGui で調整）
    LevelUpSystem           m_LevelUpSystem;
    BackpackAggregateSystem m_BackpackAggregate;
    RenderSystem            m_RenderSystem;
    SpawnDirector           m_SpawnDirector;

    // --- GPU 側 gameplay（雑魚・投射物・オーブ）---
    SwarmSystem m_Swarm;

    // --- Particle / VFX / Billboard ---
    GPUParticleSystem           m_ParticleSystem;
    ProjectileBillboardRenderer m_ProjectileRenderer;
    VFXContext                  m_VFXContext;
    std::shared_ptr<Texture>    m_ParticleTexture;
    float m_TotalTime = 0.0f;

    // --- 戦績（死亡時に RunResult へ写してリザルトへ渡す）---
    static constexpr float kDeathToResult = 2.0f;   // 死亡からリザルトまでの秒数
    float m_ExpGained = 0.0f;     // 拾った経験値の合計
    float m_DeathTimer = 0.0f;    // 死亡してからの秒数
    bool  m_RunEnded = false;     // リザルトへの切替を依頼済み

    GameUI m_GameUI;
    GridWorld m_Grid;

    // --- 画面サイズ（Graphics から毎フレーム取る）---
    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;

    // --- Entities ---
    Entity m_Player = 0;
    std::vector<Entity> m_Terrain;
    std::vector<Entity> m_Elites;      // CPU に残る敵はこれだけ

    // --- 使い回すモデル ---
    std::shared_ptr<Model> m_DummyModel;

    // --- 雑魚の初期値（SpawnDirector 経由で GPU へ渡す）---
    float m_MobHp = 30.0f;
    float m_MobSpeed = 3.5f;

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
    float m_Gravity = -25.0f;
    float m_SpawnPos[3] = { 0.0f, 5.0f, 0.0f };

    // ============================================================
    // 負荷テスト
    // ============================================================
    // ※生成先は GPU（SwarmSystem::SpawnProjectile）。
    //   弾1つにつき emitter が1つ積まれ、粒子側の経路に負荷がかかる
    int  m_StressCount = 500;
    int  m_StressPending = 0;

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

    // --- Mesh 発射の動作確認（仮設。Editor / 実体への接続ができたら外す）---
    // 玩家の胶囊 Mesh を発射源として登録し、毎フレーム世界行列を渡して粒子を出す
    bool                   m_MeshEmitTest = false;
    float                  m_MeshEmitRate = 400.0f;
    int                    m_MeshEmitSourceId = -1;
    std::shared_ptr<Model> m_MeshEmitModel;        // 登録した源（RebuildVisual での差し替え検知）
    GPUParticleEmitter     m_MeshEmitter{ 7777 };

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
    };

    static constexpr StressPreset kStressPresets[] = {
        { "P1 Starve 4000", "pool starvation: target 4000, batch 100",
          4000, 100, true },
        { "P2 Starve 1024", "target 1024 (= emitter cap), batch 100",
          1024, 100, true },
        { "P3 Refill 4000", "target 4000, batch 100",
          4000, 100, true },
        { "P4 Light 500",   "baseline: target 500, batch 50",
          500,  50,  true },
        { "C1 Steady 500",  "target 500, batch 50",
          500,  50,  true },
        { "C2 Scale 2000",  "target 2000, batch 100",
          2000, 100, true },
        { "D1 Scale 1000",  "target 1000, batch 50",
          1000, 50,  true },
    };

    void ApplyStressPreset(const StressPreset& p);
    int  m_LastPresetIndex = -1;
    bool m_ShowSwarmDebug = false;
    // --- 敵生成の調整 ---
    int      m_SpawnPointCount = 6;    // 生成点の数
    uint32_t m_TerrainSeed = 1;        // 地形の seed（ImGui から変えて Regenerate）
};