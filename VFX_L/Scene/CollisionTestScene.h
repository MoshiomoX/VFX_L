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
#include "World/GrassFireField.h"
#include "World/GroundDebris.h"
#include "ECS/System/InteractionSystem.h"
#include "ECS/System/RewardCrateSystem.h"
#include "ECS/System/PickupSystem.h"
#include "ECS/System/FeedbackVFXSystem.h"
#include "ECS/System/PlayerVFXSystem.h"
#include "ECS/System/SpellFxSystem.h"
#include "World/WeatherSystem.h"
#include "Audio/BattleAudio.h"
#include "Particle/GPUParticleSystem.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXSpriteRenderer.h"
#include "VFX_Editor/VFXBeamRenderer.h"
#include "VFX_Editor/VFXMeshRenderer.h"
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
#include "Debug/SpellLab.h"
                
#include "World/GridWorld.h"
#include "World/TerrainGenerator.h"
#include "World/MapData.h"
#include "World/StageConfig.h"
#include "Swarm/SwarmSystem.h"

#include "UI/GameUI.h"
#include "UI/TutorialGuide.h"
#include "UI/TitleScreenUI.h"
#include "UI/StageSelectUI.h"
#include "Core/GameProgress.h"
#include "Graphics/PostProcess/SceneSnapshot.h"
#include "Camera/CameraBase.h"
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

protected:
    // 魔法の実験場（SpellLabScene が立てる。Init で平地・湧きなし・無敵などに分岐する）
    bool m_LabScene = false;
    // タイトル画面（TitleScene が立てる。2026-10-09）：草原の地形・草・空はそのまま、敵・箱・門・HUD なし。
    // 丘の上に立つプレイヤーをカメラがゆっくり回り、手前に TitleScreenUI。中身は CollisionTestSceneTitle.cpp
    bool m_TitleScene = false;
    // ステージ選択（StageSelectScene が立てる。2026-10-21）：タイトルと同じ作り（選んだマップの景色をカメラが回る）で手前に StageSelectUI。
    // 中身は CollisionTestSceneSelect.cpp
    bool m_SelectScene = false;
    bool IsMenuScene() const { return m_TitleScene || m_SelectScene; }   // 敵・箱・門・HUD・自動テストを回さない画面

private:
    // ---- タイトル画面（CollisionTestSceneTitle.cpp）----
    void InitTitle();               // Init の終わり：プレイヤーを丘の上へ、夕方の光、カメラ、メニュー
    void UpdateTitle(float dt);     // Update の代わり（操作・湧き・武器は回さない）
    void UpdateTitleCamera(float dt);
    void PlaceTitleStand();         // 立つ所とカメラの向きを選んでプレイヤーを置く（m_TitleFocus / m_TitleFacing。選択画面と共用）
    void ApplyTitleLight(const SceneLighting::Preset& base);   // 午後遅くの光（太陽はカメラの側の斜めから）
    void UpdateTitleWorld(float dt, bool demo);   // 景色の更新（物理・アニメ・草・GPU・粒子）。demo = 戦いの見本をスローで回す

    // ---- ステージ選択（CollisionTestSceneSelect.cpp。2026-10-21）----
    void InitSelect();
    void UpdateSelect(float dt);
    void ApplySelectLight();        // 草原はタイトルの午後の光、他はそのマップの照明
    StageSelectUI m_SelectUI;
    std::string m_RebuildProfile;        // TEMP-TEST：RebuildTerrain の各段の ms（コンソールと select 自動テストの記録用）
    // 3 つのマップの地形を最初に全部建てておき、切替は「出す / しまう」だけ（2026-10-21 ユーザー：「切替の度に読み直すな、3 つ同時に持て」）。
    // しまう = その地形の実体から ModelComponent / ColliderComponent を外して預かる（描かれず・当たらず）、出す = 戻す。
    // 中身は CollisionTestSceneSelect.cpp（SelectWorlds）
    struct SelectWorlds;
    std::shared_ptr<SelectWorlds> m_SelectWorlds;
    void BuildSelectWorlds();            // Init で建った地形を 1 つ目として預かり、残りの 2 つも建てて預かる
    void ActivateSelectWorld(int map);   // そのマップを出して、格子・置物・草・GPU の表・立つ所・光を差し替える
    std::vector<uint8_t> m_GrassMask;    // 今の地形の草の生える所（BuildTerrain が記録。地形の出し入れで草を建て直す）

    // ---- プレイの計画（ステージ選択で決まる。2026-10-21）：m_RunStart から m_RunTier 面続ける ----
    int   m_RunStart = 1;
    int   m_RunTier = 1;
    float m_ExpMul = 1.0f;          // Tier の経験値倍率（GPU から受け取った経験値に掛ける）
    bool  HasNextStage() const { return m_StageIndex < StageConfig::kStageCount && m_StageIndex < m_RunStart + m_RunTier - 1; }
    // 戦いの見本（2026-10-09 ユーザー：「立っているだけでは意味が分からない、エフェクトが弾ける画面に」）：
    // 見栄えのする魔法を背嚢に並べ、画面の奥から雑魚・自爆兵を寄せ続け、プレイヤーが自動で撃って吹き飛ばす
    void InitTitleDemo();
    void UpdateTitleDemo(float dt);
    void SpawnTitleEnemies(int count, float minDist, float maxDist);
    float m_TitleSpawnTimer = 0.0f;
    float m_TitleMeteorTimer = 0.0f;
    TitleScreenUI m_TitleUI;
    CameraBase    m_TitleCam;
    DirectX::SimpleMath::Vector3 m_TitleFocus;   // プレイヤーの足元（カメラが回る中心）
    float         m_TitleFacing = 0.0f;          // カメラの振れの真ん中（プレイヤーから見たカメラの方角、度）。敵はこの反対（画面の奥）から来る
    float         m_TitleTime = 0.0f;
    static constexpr uint32_t kTitleSeed = 12345;   // タイトルの地形はいつも同じ（見え方を決めて置ける）

    // ---- リザルトの背景の写真（2026-10-09、Graphics/PostProcess/SceneSnapshot）----
    // 爆発の山場（HighlightPicker）か、それが無ければ力尽きた / クリアの瞬間を、UI を描く前に写す
    HighlightPicker m_Highlight;
    bool  m_SnapshotThisFrame = false;   // このフレームの Render で写す
    bool  m_SnapshotIsHighlight = false;
    bool  m_EndShotChecked = false;      // 終わった瞬間の写真（山場が無かった時）を決めた
    float m_GoldAtStart = 0.0f;          // 面の始めの WalletComponent::earned（この面で拾った金貨 = 差）
    void  OnExplosions(int count);       // 揺れ（カメラ）と山場の数え（写真）を一緒に

    // ---- フレーム処理（CollisionTestScene.cpp）----
    void UpdateScreenSize();
    void UpdateGameplay(float dt);
    float TrackPlayerHpLoss();       // このフレームに減った HP（最初のフレームは 0）
    void UpdateHudMarkers();         // 画面外の目印（報酬の箱 = 黄、エリート = 赤）
    void UpdateBossAttacks(float dt, const DirectX::SimpleMath::Vector3& player);   // Boss の技：輪・衝撃波・突進・当たり（CollisionTestSceneBoss.cpp）
    bool IsPlayerDead();
    const Vector3* PlayerPos();      // プレイヤーの位置（居なければ null）。部品へ渡す用
    void PushWind();                 // 世界の風（WeatherSystem::Wind）を草・マント・粒子・草の火へ渡す（毎フレーム、粒子の Flush の前）
    // 突風を風源にする（2026-10-20）：プレイヤーの前に渦 + 風下へ押す風（SwarmSystem::AddWindBurst）。流線の粒子・草・落ち葉・煙が巻かれる
    void AddGustWindBurst(const DirectX::SimpleMath::Vector3& pos, const DirectX::SimpleMath::Vector2& dir, float length);
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
    wchar_t                 m_ObjectiveBuf[96] = {};   // HUD の目標の一行（同上。2026-10-07）
    BackpackAggregateSystem m_BackpackAggregate;
    RenderSystem            m_RenderSystem;
    StaticPropRenderer      m_StaticProps;   // 野原の置物（ModelComponent::batched）をまとめて描く
    GrassRenderer           m_Grass;         // 野原の草（GPU で生やす葉。風・踏み跡）
    GrassFireField          m_GrassFire;     // 草の延焼（温度・燃料のテクスチャ。爆発・火球が熱源、見た目だけ。2026-10-12）
    GroundDebris            m_Debris;        // 地面の落ち葉・草屑・小枝（突風・爆発・強風で舞う。2026-10-13）

    // ---- 部品 ----
    RewardCrateSystem       m_Crates;              // 報酬の箱
    TutorialGuide           m_Tutorial;            // 初回のチュートリアル（2026-10-13。HUD の案内と目標の行に出す）
    PickupSystem            m_Pickups;             // 場の拾い物（磁石）
    FeedbackVFXSystem       m_Feedback;            // レベルアップ・開箱・被弾のエフェクト
    PlayerVFXSystem         m_PlayerVFX;           // プレイヤーの動きのエフェクト（詠唱の光・土煙・着地・尾・被弾の閃き。2026-10-09）
    SpellFxSystem           m_SpellFx;             // 魔法の CPU 側の見た目（アイススパイクの氷のトゲ。2026-10-10）
    WeatherSystem           m_Weather;             // 時刻（昼 → 夜）と天候の出来事（雨・砂嵐・雷）。照明を毎フレーム上書き
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
    SpellLab                m_SpellLab;            // 魔法を自由に組み合わせて試す実験場（2026-10-06）

    // --- GPU 側 gameplay（雑魚・投射物・オーブ）---
    SwarmSystem m_Swarm;

    // --- Particle / VFX / Billboard ---
    GPUParticleSystem           m_ParticleSystem;
    VFXContext                  m_VFXContext;
    VFXSpriteRenderer           m_SpriteRenderer;   // Sprite entry（連番画像）。レベルアップ・開箱などの CPU エフェクト
    VFXBeamRenderer             m_BeamRenderer;     // Beam entry（光線）
    VFXMeshRenderer             m_MeshRenderer;     // Mesh entry（水晶玉の光球の球など。2026-10-06 まで戦闘では無かった）
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

    // --- 地図のデータ（2026-10-05、World/MapData。地図エディタの 1 歩目）---
    // 生成でも読み込みでも、建てた物の記録を持つ（保存・エディタの元）。中身は CollisionTestSceneTerrain.cpp
    MapData::Map m_TerrainMap;
    std::string  m_MapFile;              // 読む地図の名前（空 = seed から生成）。StageDef::mapFile / VFXL_MAP / Terrain パネル
    bool         m_MapLoaded = false;    // 今の地形はファイルから建てた
    char         m_MapNameBuf[64] = "map01";   // Terrain パネルの名前欄
    // 地形を建てる：m_MapFile があれば読む（失敗したら生成）、無ければ seed から生成。実体・格子・記録・草のマス
    void BuildTerrain(std::vector<uint8_t>& grassMask);
    // 古い地形を消して建て直し、後処理（置物の描画表・草・GPU の格子表・箱）まで行う（Terrain パネル・自動テスト）
    void RebuildTerrain();
    // トレーニング（実験場）のフィールド切替（2026-10-20、ユーザー「トレーニングからフィールドに入る。モードはそのまま」）：
    // field 0 = 平地の実験場、1〜3 = その面の本番の地形（段々の台地・置物・照明・草）。プレイヤーは中央へ。
    // 無敵・MP 無限・湧き停止・箱 / 門 / 磁石なしのトレーニングのまま。戻り値 = メニューに出す一行
    std::wstring EnterTrainingField(int field, uint32_t seed = 0);   // seed 0 = 毎回違う。ステージ選択は決まった seed（2026-10-21）
    // 今ある報酬の箱と Boss の門を m_TerrainMap の placements へ（その種類がまだ 1 つも無い時だけ）
    void BakePlacements();

    // --- 面（2026-09-30）。どの面かで地形の見た目・照明・草・難度の下駄が変わる（World/StageConfig）---
    int m_StageIndex = 1;
    std::vector<DirectX::SimpleMath::Vector3> m_Torches;   // 遺跡の壁の松明（プレイヤーに近い物にだけ点光源を付ける）
    void SubmitTorchLights();
    // 前の面から引き継いだプレイヤー（g_RunCarry）を写す。引き継ぎが無ければ何もしない
    void ApplyRunCarry();
};
