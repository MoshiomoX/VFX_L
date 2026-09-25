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
#include "Player/PlayerAnimSystem.h"
#include "ECS/System/SkinnedAnimSystem.h"
#include "ECS/System/WeaponSystem.h"
#include "ECS/System/ProjectileSystem.h"
#include "ECS/System/ProjectileVFXSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "ECS/System/MeshVFXSystem.h"
#include "Graphics/Renderer/ProjectileBillboardRenderer.h"
#include "ECS/System/BackpackAggregateSystem.h"
#include "UI/LevelUpSystem.h"
#include "ECS/System/RenderSystem.h"
#include "ECS/System/InteractionSystem.h"
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

    // ---- 照明 ----
    Vector3 SunDirection() const;  // pitch / yaw → 光の進む向き（正規化済み）
    void DrawSunMarker();          // 太陽の目印 + 光の向きの矢印
    void SubmitSceneLight();       // 点光源表へ積む（Upload より前 = UpdateGameplay の前に呼ぶ）
    void DrawSceneLightGizmo();    // 3D ギズモと目印（ImGui のフレーム内で）

    // ---- 生成 / 再構築 ----
    void RebuildPlayerMesh();
    void SpawnElite(const Vector3& pos);   // CPU 側の的（無敵・動かない）
    bool AttachEliteVisual(Entity e);      // 精英に骨付きモデルを付ける（駄目なら false）
    void RespawnElites();
    void SpawnRewardCrates();              // 報酬の箱を並べ直す（開局と地形の作り直し）
    void DrawCratePanel();
    void StressSpawnProjectiles(int count);
    int  CountProjectiles() const;
    void EndRun();                         // 戦績を書いてリザルトへ
    void RegisterItemVisuals();

private:
    FollowCamera m_Camera;
    Registry     m_Registry;

    // ---- 画面の揺れのきっかけ（Camera 面板で調整）----
    // 被弾：trauma = min(max, base + 減った HP × perDamage)
    bool  m_ShakeOnHit = true;
    float m_HitTraumaBase = 0.30f;
    float m_HitTraumaPerDamage = 0.01f;
    float m_HitTraumaMax = 0.70f;
    float m_PrevPlayerHp = -1.0f;       // -1 = まだ読んでいない（最初のフレームで揺らさない）
    // 範囲攻撃：GPU の aliveAreas が増えた数 × perArea（位置は来ないので距離では弱めない）
    bool  m_ShakeOnArea = true;
    float m_AreaTrauma = 0.12f;
    float m_AreaTraumaMax = 0.35f;
    uint32_t m_PrevAliveAreas = 0;

    // ============================================================
    // Systems
    // 実行順は UpdateGameplay の並びがすべて。
    // 宣言順には意味を持たせない。
    // ============================================================
    CollisionSystem         m_CollisionSystem;
    PhysicsSystem           m_PhysicsSystem;
    PlayerControlSystem     m_PlayerControlSystem;
    PlayerStateSystem       m_PlayerStateSystem;
    PlayerAnimSystem        m_PlayerAnimSystem;    // 状態機 → クリップ名
    SkinnedAnimSystem       m_SkinnedAnimSystem;   // クリップの時計
    WeaponSystem            m_WeaponSystem;
    ManaSystem              m_ManaSystem;
    ProjectileSystem        m_ProjectileSystem;
    ProjectileVFXSystem     m_ProjectileVFXSystem;
    AreaVFXPlayer           m_AreaVFX;             // CPU から出した範囲攻撃の見た目
    int                     m_AreaTestProfile = 1; // Swarm パネルの Area Test 用
    MeshVFXSystem           m_MeshVFXSystem;       // モデル表面からの粒子（燃焼消滅など）
    float                   m_BurnDuration = 1.5f; // 燃焼消滅の秒数（ImGui で調整）
    LevelUpSystem           m_LevelUpSystem;
    InteractionSystem       m_Interaction;         // 近づいて F で使う物（報酬の箱）
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

    // --- 報酬の箱（近づいて F → 升級と同じ三択。レベルは上がらない）---
    // 開局に玩家の周りの歩けるマスへ固定数を置く。使ったら消える（補充しない）
    std::vector<Entity> m_Crates;
    std::shared_ptr<Model> m_CrateModel;
    int   m_CrateCount = 4;
    float m_CrateMinDist = 6.0f;           // 出生点からの距離（m）
    float m_CrateMaxDist = 22.0f;
    float m_CrateSpacing = 5.0f;           // 箱同士の最小間隔（m）
    float m_CrateSize = 0.9f;              // 一辺（m）。モデルの包囲箱から倍率を決める
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

    // --- 照明（主光 = 太陽。Unity の新規シーンの Directional Light に合わせた初期値）---
    // 向きは Unity の回転と同じく「見下ろす角度 pitch」と「水平の向き yaw」（度）で持つ。
    // 環境光は半球（上向きの面 = 空の色、下向きの面 = 地面の色）。
    // 高光は PS 側（Shader/Common/Lighting.hlsli）
    // Unity は左手系、こちらは右手系（SimpleMath の CreateLookAt）で X が鏡写しになる。
    // Unity の (50, -30) と画面上で同じ当たり方（カメラから見て右前上から）にするため yaw は +30
    static constexpr float kSunPitchDefault = 50.0f;
    static constexpr float kSunYawDefault = 30.0f;
    float m_SunPitch = kSunPitchDefault;
    float m_SunYaw = kSunYawDefault;
    float m_LightColor[3] = { 1.0f, 0.957f, 0.839f };
    float m_LightIntensity = 1.0f;
    float m_AmbientSky[3] = { 0.40f, 0.44f, 0.50f };
    float m_AmbientGround[3] = { 0.22f, 0.20f, 0.18f };
    bool  m_ShowSunMarker = true;   // 玩家の頭上に太陽の目印と光の向きの矢印を出す

    // --- 場景光源（位置を持つ点光源。既定は切。松明など局所の光に使う）---
    // 毎フレーム PointLightManager の先頭に積む（上限 64 の先着順でも必ず入る）。
    // 位置は Lighting 面板の数値か 3D ギズモ（左ドラッグ）で動かす。
    // PS 側は距離減衰 + 拡散 + GGX の高光（Shader/Common/Lighting.hlsli）
    bool    m_SceneLightOn = false;
    Vector3 m_SceneLightPos = { 0.0f, 6.0f, 0.0f };     // 出生点（原点）の上
    float   m_SceneLightColor[3] = { 1.0f, 0.9f, 0.75f };
    float   m_SceneLightRadius = 20.0f;                  // ここで光が 0 になる
    float   m_SceneLightIntensity = 1.2f;                // 真下の地面が平行光 1.0 の頃と同じくらい
    bool    m_SceneLightGizmo = true;                    // 3D ギズモを出す
    bool    m_SceneLightMarker = true;                   // 電球と地面の照射範囲を線で出す

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