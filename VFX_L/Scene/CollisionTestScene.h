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
//   CPU の Registry に居る敵は EliteTag（エリート・計測用の的）だけ。
//
// 出来上がった機能は部品に分けてある（シーンは持って呼ぶだけ）:
//   BattleCamera（カメラ・マウスの捕獲・揺れ）/ RewardCrateSystem（報酬の箱）/
//   FeedbackVFXSystem（レベルアップ・開箱・被弾のエフェクト）/ SceneLighting（太陽・シーン光源）/
//   EliteSpawner（エリートの的・燃焼消滅）/ MobSpawner（雑魚の湧き）/ StressTestTools（負荷テスト）
// ImGui のパネル・デバッグ描画・TEMP-TEST の自動テストは CollisionTestSceneDebug.cpp
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/BattleCamera.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"

#include "Collider/CollisionSystem.h"
#include "ECS/System/PhysicsSystem.h"
#include "Player/PlayerControlSystem.h"
#include "Player/PlayerStateSystem.h"
#include "Player/PlayerAnimSystem.h"
#include "ECS/System/SkinnedAnimSystem.h"
#include "ECS/System/ClothChainSystem.h"
#include "ECS/System/WeaponSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "ECS/System/MeshVFXSystem.h"
#include "ECS/System/BackpackAggregateSystem.h"
#include "UI/LevelUpSystem.h"
#include "ECS/System/RenderSystem.h"
#include "Graphics/Renderer/StaticPropRenderer.h"
#include "Graphics/Renderer/GrassRenderer.h"
#include "ECS/System/InteractionSystem.h"
#include "ECS/System/RewardCrateSystem.h"
#include "ECS/System/PickupSystem.h"
#include "ECS/System/FeedbackVFXSystem.h"
#include "Audio/BattleAudio.h"
#include "Particle/GPUParticleSystem.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXSpriteRenderer.h"
#include "VFX_Editor/VFXBeamRenderer.h"
#include "VFX_Editor/VFXLiquidRenderer.h"
#include "ECS/System/ManaSystem.h"
#include "Enemy/EliteSpawner.h"
#include "Enemy/MobSpawner.h"
#include "Enemy/StageDirector.h"
#include "Enemy/BossAttacks.h"
#include "Graphics/Light/SceneLighting.h"
#include "Graphics/PostProcess/Outline.h"
#include "Graphics/Light/ShadowMap.h"
#include "Debug/StressTestTools.h"
                
#include "World/GridWorld.h"
#include "World/TerrainGenerator.h"
#include "World/StageConfig.h"
#include "Swarm/SwarmSystem.h"

#include "UI/GameUI.h"
#include "SpellID.h"      // ItemID
#include <memory>
#include <vector>

class BattleAutoTest;   // TEMP-TEST: 自動テスト（Debug/AutoTest/）

class CollisionTestScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    // ---- フレーム処理（CollisionTestScene.cpp）----
    void UpdateScreenSize();
    void UpdateGameplay(float dt);
    float TrackPlayerHpLoss();       // このフレームに減った HP（最初のフレームは 0）
    void UpdateHudMarkers();         // 画面外の目印（報酬の箱 = 黄、エリート = 赤）
    void UpdateBossAttacks(float dt, const DirectX::SimpleMath::Vector3& player);   // Boss のスラム：輪・爆発・当たり
    bool IsPlayerDead();
    const Vector3* PlayerPos();      // プレイヤーの位置（居なければ null）。部品へ渡す用
    void RespawnCrates();            // 報酬の箱をプレイヤーの周りへ並べ直す（開始時・地形の作り直し・パネル）
    void RespawnElites();            // エリートの的をプレイヤーの前へ（地形の作り直し・パネル）
    void EndRun();                   // 戦績を書いてリザルトへ

    // ---- ImGui / デバッグ描画（CollisionTestSceneDebug.cpp）----
    void DrawDebugUI();
    void DrawPlayerPanel();
    void DrawWandPanel();
    void DrawSwarmPanel();
    void DrawItemDatabasePanel();
    void DrawTerrainPanel();
    void DrawEnemiesPanel();
    void DrawBloomPanel();   // 後処理 bloom の調整（Graphics 側の BloomParams を直接触る）
    void DrawGameplayDebug();   // 衝突体のワイヤ・杖・格子（UpdateGameplay の最後）
    void DrawColliderDebug(Entity e, const Color& color);
    void DrawWandDebug();
    void RebuildPlayerMesh();

    // ---- TEMP-TEST: VFXL_BATTLE_AUTOTEST=<名前>（自動テスト）----
    // 自動テストの中身は Debug/AutoTest/ に 1 本 1 ファイル（BattleAutoTest を継ぐ）。シーンは名前で 1 つ作って回すだけ。
    // 出来事は実時間（ms）付きで autotest.log へ（画面の連写と突き合わせる）
    friend class BattleAutoTest;
    bool  m_AutoTest = false;
    int   m_AutoStep = 0;
    float m_AutoTime = 0.0f;
    bool  m_AutoInteract = false;   // true = このフレームに F を押した扱い
    std::shared_ptr<BattleAutoTest> m_AutoRunner;
    void  AutoTestLog(const char* what);
    void  UpdateAutoTest(float dt);   // gameplay の最後から

private:
    BattleCamera m_Camera;   // FollowCamera は m_Camera.Camera()
    Registry     m_Registry;
    float        m_PrevPlayerHp = -1.0f;   // -1 = まだ読んでいない（最初のフレームで揺らさない）
    bool         m_PrevDebugGrid = true;   // 入る前の DebugManager の参照格子（Shutdown で戻す）
    uint32_t     m_PortalVfx = 0;          // Boss の門の渦（AreaVFXPlayer のインスタンス。0 = 無し）

    // 置物（報酬の箱・門の柱）の下で雑魚用に塞いだマス（2026-10-03）。元は歩けたマスだけ覚え、
    // 箱が開いて消えたら戻す。crate = 箱（InteractableComponent が外れたら消えた扱い）
    struct PropBlock { Entity entity; bool crate; std::vector<int> cells; };
    std::vector<PropBlock> m_PropBlocks;
    void BlockPropCells();     // RespawnCrates の最後（前の分は戻してから塞ぎ直す）
    void UpdatePropBlocks();   // 毎フレーム：消えた箱の分を戻す
    void BlockUnreachablePockets();   // 地形の直後：フィールドの真ん中へ歩いて行けないマスを塞ぐ

    // ============================================================
    // Systems
    // 実行順は UpdateGameplay の並びがすべて。
    // 宣言順には意味を持たせない。
    // ============================================================
    CollisionSystem         m_CollisionSystem;
    PhysicsSystem           m_PhysicsSystem;
    PlayerControlSystem     m_PlayerControlSystem;
    PlayerStateSystem       m_PlayerStateSystem;
    PlayerAnimSystem        m_PlayerAnimSystem;    // ステートマシン → クリップ名
    SkinnedAnimSystem       m_SkinnedAnimSystem;   // クリップの時計
    ClothChainSystem        m_ClothChainSystem;    // マントの揺れ（2026-10-04）
    WeaponSystem            m_WeaponSystem;
    ManaSystem              m_ManaSystem;
    AreaVFXPlayer           m_AreaVFX;             // CPU から出した範囲攻撃・反応のエフェクトの見た目
    int                     m_AreaTestProfile = 1; // Swarm パネルの Area Test 用
    MeshVFXSystem           m_MeshVFXSystem;       // モデル表面からの粒子（燃焼消滅など）
    LevelUpSystem           m_LevelUpSystem;
    InteractionSystem       m_Interaction;         // 近づいて F で使う物（報酬の箱）
    std::wstring            m_PromptBuf;           // 報酬の箱の案内（値段入り。GameUI はポインタを持つので文字列はここに置く）
    BackpackAggregateSystem m_BackpackAggregate;
    RenderSystem            m_RenderSystem;
    StaticPropRenderer      m_StaticProps;   // 野原の置物（ModelComponent::batched）をまとめて描く
    GrassRenderer           m_Grass;         // 野原の草（GPU で生やす葉。風・踏み跡）

    // ---- 部品 ----
    RewardCrateSystem       m_Crates;              // 報酬の箱
    PickupSystem            m_Pickups;             // 場の拾い物（磁石）
    FeedbackVFXSystem       m_Feedback;            // レベルアップ・開箱・被弾のエフェクト
    BattleAudio             m_Audio;               // 戦闘の音（GPU の範囲・プレイヤーの動き・BGM。2026-10-03）
    SceneLighting           m_Lighting;            // 太陽・環境光・シーン光源
    Outline                 m_Outline;             // トゥーンのアウトライン（2026-10-04。不透明な物の後、草の前）
    ShadowMap               m_Shadows;             // 太陽の影（3 段のカスケード）
    EliteSpawner            m_Elites;              // エリートの的（CPU）
    MobSpawner              m_Mobs;                // 雑魚の湧き（GPU へ依頼）
    StageDirector           m_Stage;               // 1 面の進行（制限時間・エリートの時間表）
    BossAttacks             m_BossAttacks;         // Boss の技（スラムの警告の輪。2026-10-03）
    uint32_t                m_BossSlamHits = 0;    // 自動テストの記録用：スラムがプレイヤーに当たった数
    StressTestTools         m_Stress;              // 負荷テスト・Mesh 発射の確認

    // --- GPU 側 gameplay（雑魚・投射物・オーブ）---
    SwarmSystem m_Swarm;

    // --- Particle / VFX / Billboard ---
    GPUParticleSystem           m_ParticleSystem;
    VFXContext                  m_VFXContext;
    VFXSpriteRenderer           m_SpriteRenderer;   // Sprite entry（連番画像）。レベルアップ・開箱などの CPU エフェクト
    VFXBeamRenderer             m_BeamRenderer;     // Beam entry（光線）
    VFXLiquidRenderer           m_LiquidRenderer;   // Liquid entry（CPU で出す液溜まり。GPU の範囲の物は m_Swarm が描く）
    std::shared_ptr<Texture>    m_ParticleTexture;
    float m_TotalTime = 0.0f;
    float m_RunTime = 0.0f;       // 遊んでいる時間（止まっている間・死んだ後は進まない）。HUD とリザルト用

    // --- 戦績（死亡時に RunResult へ写してリザルトへ渡す）---
    static constexpr float kDeathToResult = 3.0f;   // 死亡からリザルトまでの秒数（「力尽きた」の幕を 2 秒ほど見せる）
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

    // --- 表示切替 ---
    // デバッグの線（衝突体・杖・格子）は既定で切（2026-09-28。Debug で 0.7 ms。パネルの Game Test から入れる）
    bool m_ShowWireframe = false;
    bool m_ShowMesh = true;
    bool m_ShowWandDebug = false;
    bool m_ShowGridDebug = false;   // プレイヤーの周りの格子（通行・流れ場）
    bool m_ShowSwarmDebug = false;

    // ※粒子だけを個別に消せるようにしておく。
    //   負荷の出どころが粒子かどうかを切り分けるため。
    bool m_ShowParticle = true;

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

    // --- 地形 ---
    // 地形の設定。seed は開始時ごとに乱数（環境変数 VFXL_TERRAIN_SEED があればその値。再現用）。
    // Terrain パネルから変えて Regenerate
    TerrainGenerator::Config m_TerrainConfig;
    // 三層（山頂・洞窟）のマス。箱を山頂・洞窟にも置き、Boss の門を洞窟の奥に置く
    TerrainGenerator::Layout m_TerrainLayout;

    // --- 面（2026-09-30）。どの面かで地形の見た目・照明・草・難度の下駄が変わる（World/StageConfig）---
    int m_StageIndex = 1;
    std::vector<DirectX::SimpleMath::Vector3> m_Torches;   // 遺跡の壁の松明（プレイヤーに近い物にだけ点光源を付ける）
    void SubmitTorchLights();
    // 前の面から引き継いだプレイヤー（g_RunCarry）を写す。引き継ぎが無ければ何もしない
    void ApplyRunCarry();
};
