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
//
// 出来上がった機能は部品に分けてある（シーンは持って呼ぶだけ）:
//   BattleCamera（カメラ・マウスの捕獲・揺れ）/ RewardCrateSystem（報酬の箱）/
//   FeedbackVFXSystem（升級・開箱・被弾の特効）/ SceneLighting（太陽・場景光源）/
//   EliteSpawner（精英の的・燃焼消滅）/ MobSpawner（雑魚の湧き）/ StressTestTools（負荷テスト）
// ImGui の面板・デバッグ描画・TEMP-TEST の自測は CollisionTestSceneDebug.cpp
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
#include "Particle/GPUParticleSystem.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXSpriteRenderer.h"
#include "VFX_Editor/VFXBeamRenderer.h"
#include "ECS/System/ManaSystem.h"
#include "Enemy/EliteSpawner.h"
#include "Enemy/MobSpawner.h"
#include "Enemy/StageDirector.h"
#include "Graphics/Light/SceneLighting.h"
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
    void UpdateHudMarkers();         // 画面外の目印（報酬の箱 = 黄、精英 = 赤）
    bool IsPlayerDead();
    const Vector3* PlayerPos();      // 玩家の位置（居なければ null）。部品へ渡す用
    void RespawnCrates();            // 報酬の箱を玩家の周りへ並べ直す（開局・地形の作り直し・面板）
    void RespawnElites();            // 精英の的を玩家の前へ（地形の作り直し・面板）
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

    // ---- TEMP-TEST: VFXL_BATTLE_AUTOTEST（CollisionTestSceneDebug.cpp）----
    // 5 秒で升級分の経験値、9 秒で最寄りの箱の横へ、10 秒で F を押した扱い。
    // 出来事は実時間（ms）付きで autotest.log へ（画面の連写と突き合わせる）
    bool  m_AutoTest = false;
    int   m_AutoStep = 0;
    float m_AutoTime = 0.0f;
    bool  m_AutoInteract = false;
    void  AutoTestLog(const char* what);
    void  UpdateAutoTest(float dt);
    // VFXL_BATTLE_AUTOTEST=bomber：施法を止め、湧きを止めて玩家の近くに自爆兵を出し、
    // GPU の counter（活き数・撃破・玩家への累計ダメージ・範囲数）と HP が変わる度に記録する
    bool     m_AutoBomber = false;
    uint32_t m_AutoLast[4] = {};   // aliveEnemies / killCount / playerDamage / aliveAreas
    float    m_AutoLastHp = -1.0f;
    void     UpdateAutoTestBomber();
    // VFXL_BATTLE_AUTOTEST=slide：一番長い下り坂の上に玩家を置き、走る → 滑る → 跳ぶ を入力の代わりに流して
    // 0.1 秒毎の速さ・足元の傾き・状態を記録する。続けて平地でも滑る
    bool     m_AutoSlide = false;
    void     UpdateAutoTestSlide(float dt);
    // VFXL_BATTLE_AUTOTEST=perf：野原の置物を隠す / 調試表示を切る段を順に回し、段毎の平均 fps を記録する
    bool     m_AutoPerf = false;
    void     UpdateAutoTestPerf();
    // VFXL_BATTLE_AUTOTEST=stress：玩家は動かず無敵、法術を背包に置いて撃たせ、雑魚の数を段ごとに増やす
    // （0 / 250 / 500 / 1000 / 2000 / 4000 / 4000 + 弾 2000）。段毎の fps と CPU / GPU の内訳を記録する
    bool     m_AutoStress = false;
    void     UpdateAutoTestStress();
    // VFXL_BATTLE_AUTOTEST=magnifier：拡大鏡の斜めに火球と隕石、効かない所にもう 1 つ火球を置いて撃たせ、
    // 集約後の半径・消費と道具説明を記録する（大きさの見比べは外から連写）
    bool     m_AutoMagnifier = false;
    void     UpdateAutoTestMagnifier();
    // VFXL_BATTLE_AUTOTEST=ui：背包 → tooltip → 一時停止 → HUD → 三択 を順に開いて記録する（画面は外から連写）。
    // 背包・一時停止で gameplay が止まるので Update から直接呼ぶ
    bool     m_AutoUI = false;
    void     UpdateAutoTestUI(float dt);
    // VFXL_BATTLE_AUTOTEST=loco：前 / 右 / 後 / 左 / 左後 / 右前 / 前（ゆっくり）へ走らせて
    // 脚の向き・後ろ走り・歩様・再生速度を記録し、続けて既定の鏡頭で火球と隕石の爆発を映す（画面は外から連写）
    bool     m_AutoLoco = false;
    void     UpdateAutoTestLoco(float dt);
    // VFXL_BATTLE_AUTOTEST=balance：無敵・入力なしで普通に遊ばせ（三択は先頭を自動で選ぶ）、
    // 5 秒毎に経過時間・雑魚数・撃破・等級・HP・難度の倍率を記録。
    // 10 秒で経過時間を 170 秒（3:00 の精英が出る）、30 秒で 5 分、45 秒で 10 分（時間切れ）、55 秒で 11 分へ飛ばす
    bool     m_AutoBalance = false;
    void     UpdateAutoTestBalance(float dt);
    // VFXL_BATTLE_AUTOTEST=boss：無敵・湧き停止、門の前へ移って F（Boss の HP は流れを見るため 300）、
    // 毎秒 Boss の状態を記録。倒せば「ステージクリア」→ リザルト
    bool     m_AutoBoss = false;
    void     UpdateAutoTestBoss();
    // VFXL_BATTLE_AUTOTEST=pickup：4 択の画面 → 跳躍回数（空中 2 回、12 → 9 → 6.75）→ 磁石で場の球を全部吸う、を記録
    bool     m_AutoPickup = false;
    void     UpdateAutoTestPickup(float dt);
    // VFXL_BATTLE_AUTOTEST=assets：新しい素材（沙漠・遺跡）を 1 包ずつ玩家の前に並べ、大きさ（m）を記録して撮る
    // VFXL_BATTLE_AUTOTEST=arrow：黄金の矢だけを背包に置き、正面の的へ撃たせて横から連写する
    bool     m_AutoArrow = false;
    void     UpdateAutoTestArrow();
    // VFXL_BATTLE_AUTOTEST=chain：火球 + 石弾 → 隕石の誘発。隕石が落ちるか、石弾を外すと止まるか
    bool     m_AutoChain = false;
    void     UpdateAutoTestChain();
    // VFXL_BATTLE_AUTOTEST=chest：魔法書の木箱へ形の違う物をまとめて降らせ、積み方・眠るまでを記録して撮る。
    // 背包が開いて gameplay が止まるので Update から直接呼ぶ
    bool     m_AutoChest = false;
    void     UpdateAutoTestChest(float dt);
    // VFXL_BATTLE_AUTOTEST=beam：追尾弾 + 弧 → 魔導光線の誘発。正面の的へ向けて光線が出るか（横から撮る）
    bool     m_AutoBeam = false;
    void     UpdateAutoTestBeam();
    // VFXL_BATTLE_AUTOTEST=stuck：雑魚をわざと塞がったマスの中に出し、壁から出てくるか・二度と入らないかを敵の池の読み戻しで数える
    bool     m_AutoStuck = false;
    void     UpdateAutoTestStuck();
    // VFXL_BATTLE_AUTOTEST=clip：玩家を木 / 岩 / 外周 / 崖 / 台地の箱 / 崖（精英）の横に立たせて雑魚を群がらせ、
    // 体（半径）が塞がったマス・崖へどれだけ食い込むかを数えて撮る（2026-10-01、壁に嵌まって見える件の切り分け）
    bool     m_AutoClip = false;
    void     UpdateAutoTestClip();
    // VFXL_BATTLE_AUTOTEST=knock：止まった玩家を雑魚 1 体に殴らせ、次に自爆兵 1 体を爆発させて、ノックバックで動いた距離と高さを記録
    bool     m_AutoKnock = false;
    void     UpdateAutoTestKnock();
    // VFXL_BATTLE_AUTOTEST=ghost：最終波の幽霊を 20 体出して、数・壁抜け・近づく速さを記録し撮る
    bool     m_AutoGhost = false;
    void     UpdateAutoTestGhost();
    // VFXL_BATTLE_AUTOTEST=edge：外周の岩山を、縁の近くの平視・高い所からの俯瞰・場地の中央から撮る
    bool     m_AutoEdge = false;
    void     UpdateAutoTestEdge();
    bool     m_AutoAssets = false;
    void     UpdateAutoTestAssets();
    std::vector<Entity> m_AutoAssetEntities;
    void     SetDecorPropsVisible(bool visible, int* outCount);

private:
    BattleCamera m_Camera;   // FollowCamera は m_Camera.Camera()
    Registry     m_Registry;
    float        m_PrevPlayerHp = -1.0f;   // -1 = まだ読んでいない（最初のフレームで揺らさない）

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
    AreaVFXPlayer           m_AreaVFX;             // CPU から出した範囲攻撃・反応の特効の見た目
    int                     m_AreaTestProfile = 1; // Swarm パネルの Area Test 用
    MeshVFXSystem           m_MeshVFXSystem;       // モデル表面からの粒子（燃焼消滅など）
    LevelUpSystem           m_LevelUpSystem;
    InteractionSystem       m_Interaction;         // 近づいて F で使う物（報酬の箱）
    BackpackAggregateSystem m_BackpackAggregate;
    RenderSystem            m_RenderSystem;
    StaticPropRenderer      m_StaticProps;   // 野原の置物（ModelComponent::batched）をまとめて描く
    GrassRenderer           m_Grass;         // 野原の草（GPU で生やす葉。風・踏み跡）

    // ---- 部品 ----
    RewardCrateSystem       m_Crates;              // 報酬の箱
    PickupSystem            m_Pickups;             // 場の拾い物（磁石）
    FeedbackVFXSystem       m_Feedback;            // 升級・開箱・被弾の特効
    SceneLighting           m_Lighting;            // 太陽・環境光・場景光源
    ShadowMap               m_Shadows;             // 太陽の影（3 段の級聯）
    EliteSpawner            m_Elites;              // 精英の的（CPU）
    MobSpawner              m_Mobs;                // 雑魚の湧き（GPU へ依頼）
    StageDirector           m_Stage;               // 1 面の進行（制限時間・精英の時間表）
    StressTestTools         m_Stress;              // 負荷テスト・Mesh 発射の確認

    // --- GPU 側 gameplay（雑魚・投射物・オーブ）---
    SwarmSystem m_Swarm;

    // --- Particle / VFX / Billboard ---
    GPUParticleSystem           m_ParticleSystem;
    VFXContext                  m_VFXContext;
    VFXSpriteRenderer           m_SpriteRenderer;   // Sprite entry（連番絵）。升級・開箱などの CPU 特効
    VFXBeamRenderer             m_BeamRenderer;     // Beam entry（光線）
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
    // 調試の線（衝突体・杖・格子）は既定で切（2026-09-28。Debug で 0.7 ms。面板の Game Test から入れる）
    bool m_ShowWireframe = false;
    bool m_ShowMesh = true;
    bool m_ShowWandDebug = false;
    bool m_ShowGridDebug = false;   // 玩家の周りの格子（通行・流れ場）
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
    // 地形の設定。seed は開局ごとに乱数（環境変数 VFXL_TERRAIN_SEED があればその値。再現用）。
    // Terrain 面板から変えて Regenerate
    TerrainGenerator::Config m_TerrainConfig;

    // --- 面（2026-09-30）。どの面かで地形の見た目・照明・草・難度の下駄が変わる（World/StageConfig）---
    int m_StageIndex = 1;
    std::vector<DirectX::SimpleMath::Vector3> m_Torches;   // 遺跡の壁の松明（玩家に近い物にだけ点光源を付ける）
    void SubmitTorchLights();
    // 前の面から引き継いだ玩家（g_RunCarry）を写す。引き継ぎが無ければ何もしない
    void ApplyRunCarry();
};
