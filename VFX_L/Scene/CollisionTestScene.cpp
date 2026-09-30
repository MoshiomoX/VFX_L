// ============================================================
// CollisionTestScene.cpp
// 初期化・毎フレームの流れ・描画・結算。
// ImGui の面板とデバッグ描画は CollisionTestSceneDebug.cpp
// ============================================================
#include "Scene/CollisionTestScene.h"

#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/HealthComponent.h"
#include "Component/InteractableComponent.h"
#include "Player/PlayerTag.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerFactory.h"
#include "Player/LevelComponent.h"
#include "Enemy/EnemyTags.h"
#include "ECS/View.h"
#include "Item/ItemDatabase.h"
#include "VFX_Editor/VFXId.h"
#include "VFX_Editor/VFXDatabase.h"
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"
#include "World/TerrainGenerator.h"
#include "Scene/RunResult.h"
#include "Debug/DebugManager.h"
#include "Debug/FrameProfiler.h"
#include "Manager/InputMap.h"
#include "Manager/ResourceManager.h"
#include "Core/Application.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

// ============================================================
// Init
// ============================================================
void CollisionTestScene::Init()
{
    std::cout << "[CollisionTestScene] Init" << std::endl;

    auto& gfx = Application::Get().GetGraphics();
    auto* device = gfx.GetDevice();
    auto* context = gfx.GetContext();

    // ---------- 画面サイズを最初に確定させる ----------
    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // ---------- アイテム定義の登録（他より先に行う）----------
    ItemDatabase::Initialize();

    // ---------- Camera（遮蔽の射線は地形の衝突へ）----------
    m_Camera.Init(m_ScreenW / m_ScreenH, &m_CollisionSystem);
    SetCamera(&m_Camera.Camera());
    {
        char env[16] = {};   // TEMP-TEST
        m_AutoTest = GetEnvironmentVariableA("VFXL_BATTLE_AUTOTEST", env, sizeof(env)) > 0;
        m_AutoBomber = m_AutoTest && strcmp(env, "bomber") == 0;   // 値が bomber なら自爆兵の自測
        m_AutoPerf = m_AutoTest && strcmp(env, "perf") == 0;       // 値が perf なら負荷の内訳
        m_AutoSlide = m_AutoTest && strcmp(env, "slide") == 0;     // 値が slide なら滑りの自測
        m_AutoStress = m_AutoTest && strcmp(env, "stress") == 0;   // 値が stress なら雑魚を増やしていく負荷試験
        m_AutoMagnifier = m_AutoTest && strcmp(env, "magnifier") == 0;   // 値が magnifier なら拡大鏡の見比べ
        m_AutoUI = m_AutoTest && strcmp(env, "ui") == 0;                 // 値が ui なら幻想 UI の各画面を順に開く
        m_AutoLoco = m_AutoTest && strcmp(env, "loco") == 0;             // 値が loco なら横 / 後ろ走りと爆発の見え方
        m_AutoBalance = m_AutoTest && strcmp(env, "balance") == 0;       // 値が balance なら難度の推移を記録
        m_AutoBoss = m_AutoTest && strcmp(env, "boss") == 0;             // 値が boss なら門 → Boss → クリア
        m_AutoPickup = m_AutoTest && strcmp(env, "pickup") == 0;         // 値が pickup なら 4 択・空中跳び・磁石
        m_AutoAssets = m_AutoTest && strcmp(env, "assets") == 0;         // 値が assets なら新しい素材の並べ見
        m_AutoEdge = m_AutoTest && strcmp(env, "edge") == 0;             // 値が edge なら外周の岩山を撮る
        m_AutoArrow = m_AutoTest && strcmp(env, "arrow") == 0;           // 値が arrow なら黄金の矢を横から撮る
        m_AutoChain = m_AutoTest && strcmp(env, "chain") == 0;           // 値が chain なら火球 + 石弾 → 隕石の誘発
        m_AutoChest = m_AutoTest && strcmp(env, "chest") == 0;           // 値が chest なら魔法書の木箱の物理
        m_AutoBeam = m_AutoTest && strcmp(env, "beam") == 0;             // 値が beam なら追尾弾 + 弧 → 魔導光線
        m_AutoStep = 0;
        m_AutoTime = 0.0f;
        if (m_AutoTest) AutoTestLog("start");
    }

    // ---------- Particle System ----------
    if (!m_ParticleSystem.Initialize(device, context, 100000))
        std::cout << "[Error] ParticleSystem init failed" << std::endl;

    m_ParticleSystem.SetCamera(&m_Camera.Camera());

    m_ParticleTexture = ResourceManager::Get().LoadTexture(Res::Tex::ParticleSheet);
    if (m_ParticleTexture)
        m_ParticleSystem.SetTexture(m_ParticleTexture);

    m_VFXContext.particleSystem = &m_ParticleSystem;
    if (!m_SpriteRenderer.Initialize(device))
        std::cout << "[Error] VFXSpriteRenderer init failed" << std::endl;
    m_VFXContext.spriteRenderer = &m_SpriteRenderer;
    if (!m_BeamRenderer.Initialize(device))
        std::cout << "[Error] VFXBeamRenderer init failed" << std::endl;
    m_VFXContext.beamRenderer = &m_BeamRenderer;

    // ---------- 投射物ビルボード ----------
    if (!m_ProjectileRenderer.Initialize(device, context, 4096))
        std::cout << "[Error] ProjectileRenderer init failed" << std::endl;
    m_ProjectileRenderer.SetTexture(
        ResourceManager::Get().LoadTexture(Res::Tex::ProjectileCore));

    // ---------- 見た目 と 各 System が使う VFX の登録 ----------
    // 飛行物の見た目は投射物プロファイルにあるので先に読む（GPU の表は下でもう一度 Build）
    ProjectileProfileDB::LoadAll();
    RegisterItemVisuals();

    // 燃焼消滅（Mesh 発射 + 溶解の縁）。道具ではないので VFXDatabase から直接引く
    if (const char* path = VFXDatabase::GetPath(VFXId::DeathBurn))
        m_MeshVFXSystem.RegisterVFX(VFXId::DeathBurn, path);

    // ---------- UI ----------
    // ※ItemDatabase::Initialize の後（LoadIcons が定義を読む）
    if (!m_GameUI.Initialize(device, context, m_ScreenW, m_ScreenH))
        std::cout << "[Error] GameUI init failed" << std::endl;

    // ---------- 精英の的（骨付きモデルが読めない時のカプセルを用意）----------
    // 雑魚のモデルは SwarmSystem が自前で持つ。ここには置かない
    m_Elites.Init(device);
    m_Lighting.Init(device);   // 空のシェーダー
    if (!m_Shadows.Initialize(device))
        std::cout << "[Error] ShadowMap init failed" << std::endl;

    // ---------- 地形（格子对齐。台地と坂道の野原）----------
    // 場地: 100 x 100 マス = 200m x 200m。
    // 開局ごとに seed を変える（同じ seed なら同じ地形。VFXL_TERRAIN_SEED で固定できる）
    m_Grid.Init(100, 100);
    {
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_TERRAIN_SEED", env, sizeof(env)) > 0)
            m_TerrainConfig.seed = (uint32_t)strtoul(env, nullptr, 10);
        else
            m_TerrainConfig.seed = std::random_device{}() % 100000u;
    }
    std::vector<uint8_t> grassMask;
    TerrainGenerator::Generate(m_Registry, device, m_Grid, m_TerrainConfig, m_Terrain, &grassMask);
    // 置物（木・岩・茂み）はモデル毎の instanced 描画へ
    if (!m_StaticProps.Initialize(device))
        std::cout << "[Error] StaticPropRenderer init failed" << std::endl;
    m_StaticProps.Build(m_Registry);
    // 草（GPU で生やす葉）
    if (!m_Grass.Initialize(device, context))
        std::cout << "[Error] GrassRenderer init failed" << std::endl;
    m_Grass.Build(m_Grid, grassMask, m_TerrainConfig.seed);

    // ---------- GPU 側 gameplay（雑魚・投射物・オーブ）----------
    // ※地形の生成後に呼ぶ。格子表をそのまま上げるため
    m_Swarm.SetParticleSystem(&m_ParticleSystem);
    if (!m_Swarm.Initialize(device, context))
        std::cout << "[Error] SwarmSystem init failed" << std::endl;

    m_Swarm.UploadTerrain(m_Grid);
    m_Swarm.BuildVFXTable();
    // 範囲攻撃と投射物の飛び方（編集器で作った json）を GPU の表へ。
    // 先に範囲：投射物の「命中で出す範囲」が範囲の番号を引くため
    AreaProfileDB::LoadAll();
    m_Swarm.SetAreaDefs(AreaProfileDB::BuildDefs(m_Swarm.GetVFXTable()));
    ProjectileProfileDB::LoadAll();
    m_Swarm.SetMotions(ProjectileProfileDB::BuildMotions());
    m_WeaponSystem.SetAreaVFX(&m_AreaVFX, &m_VFXContext);
    m_Mobs.Init(m_Swarm);
    m_WeaponSystem.SetSwarm(&m_Swarm);

    // ============================================================
    // プレイヤー
    // 組み立ては PlayerFactory に任せる。
    // シーンはどの Component が付いているかを知らなくてよい。
    // ============================================================
    PlayerFactory::Config pcfg;
    pcfg.color = { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f };

    m_Player = PlayerFactory::Create(m_Registry, device, pcfg);

    // ---------- 反応の特効（升級・開箱・被弾）----------
    m_Feedback.Init(&m_AreaVFX, &m_VFXContext);
    if (m_AutoTest)
        m_Feedback.onPlayed = [this](const char* file) { AutoTestLog(file); };   // TEMP-TEST

    // ---------- 報酬の箱（玩家の周りに固定数。玩家の位置が要るので最後）----------
    m_Crates.Init();
    m_Stage.Init();
    m_Pickups.Init(device);
    RespawnCrates();
}

// ============================================================
// 見た目と VFX を System に登録する
// アイテム定義側にある値をそのまま流す（ID ごとの対応表を作る）
// ============================================================
void CollisionTestScene::RegisterItemVisuals()
{
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        // --- 飛行物型: ビルボードの芯 + VFX（見た目は投射物プロファイル側）---
        if (auto* p = ItemDatabase::GetProjectile(id))
        {
            const ProjectileProfile& pp = ProjectileProfileDB::At(ProjectileProfileDB::IndexOf(p->profile));
            m_WeaponSystem.SetProjectileVisual(id,
                pp.visualSize, p->common.color, pp.visualStretch);

            // CPU 経路（精英の弾など）は今まで通り VFXEffect を張る。
            // パスは VFXDatabase から引く
            if (const char* path = VFXDatabase::GetPath(pp.ResolveVFX()))
                m_ProjectileVFXSystem.RegisterVFX(id, path);
        }

        // --- AOE 型: VFX のみ（AreaSystem は未実装）---
        if (auto* a = ItemDatabase::GetArea(id))
        {
            if (const char* path = VFXDatabase::GetPath(a->vfxId))
                m_ProjectileVFXSystem.RegisterVFX(id, path);
        }
    }
}

// ============================================================
// Shutdown
// ============================================================
void CollisionTestScene::Shutdown()
{
    SceneLighting::ClearFog(Application::Get().GetRenderer());   // Renderer は他の場面と共有
    m_Shadows.Disable(Application::Get().GetRenderer());         // 同上（影を切らないと他の場面が真っ暗）
    m_Shadows.Unbind(Application::Get().GetGraphics().GetContext());
    m_Shadows.Shutdown();
    m_Swarm.Shutdown();
    m_StaticProps.Shutdown();
    m_Grass.Shutdown();
    m_GameUI.Shutdown();
    m_ProjectileRenderer.Shutdown();
    std::cout << "[CollisionTestScene] Shutdown" << std::endl;
}

// ============================================================
// 画面サイズの変化に追従する
// UI は全部ピクセル指定なので、変わった時だけ組み直す。
// ============================================================
void CollisionTestScene::UpdateScreenSize()
{
    auto& gfx = Application::Get().GetGraphics();
    float w = gfx.GetWidth();
    float h = gfx.GetHeight();

    if (w == m_ScreenW && h == m_ScreenH) return;
    if (w <= 0.0f || h <= 0.0f) return;

    m_ScreenW = w;
    m_ScreenH = h;

    m_GameUI.Layout(w, h);
    m_Camera.Resize(w / h);

    std::cout << "[CollisionTestScene] screen resized: "
        << (int)w << "x" << (int)h << std::endl;
}

// ============================================================
// 小さな問い合わせ
// ============================================================
bool CollisionTestScene::IsPlayerDead()
{
    return m_Registry.IsValid(m_Player) && m_Registry.Has<PlayerStateComponent>(m_Player)
        && m_Registry.Get<PlayerStateComponent>(m_Player).IsDead();
}

const Vector3* CollisionTestScene::PlayerPos()
{
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<TransformComponent>(m_Player)) return nullptr;
    return &m_Registry.Get<TransformComponent>(m_Player).position;
}

// 被弾：HP の減りを見る。雑魚（GPU）・精英の接触・デバッグの被弾、経路を問わず拾える。
// 無敵中は TryApplyHit が HP を減らさないので、揺れ・斬撃も自然に止まる
float CollisionTestScene::TrackPlayerHpLoss()
{
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<HealthComponent>(m_Player)) return 0.0f;
    const float hpNow = m_Registry.Get<HealthComponent>(m_Player).current;
    const float lost = (m_PrevPlayerHp >= 0.0f) ? m_PrevPlayerHp - hpNow : 0.0f;
    m_PrevPlayerHp = hpNow;
    return (lost > 0.0f) ? lost : 0.0f;
}

void CollisionTestScene::RespawnCrates()
{
    const Vector3* p = PlayerPos();
    m_Crates.Spawn(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed, m_Interaction);
    // Boss を呼ぶ門・磁石も同じ時に置き直す（地形が変わると前の場所は歩けないかもしれない）
    m_Stage.SpawnPortal(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed, m_Interaction);
    m_Pickups.Reset(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed);
}

void CollisionTestScene::RespawnElites()
{
    m_Elites.Respawn(m_Registry, PlayerPos());
}

// ============================================================
// Update
// ============================================================
void CollisionTestScene::Update(float dt)
{
    SceneBase::Update(dt);
    m_TotalTime += dt;

    UpdateScreenSize();

    // ---- UI（開閉・入力・プレイヤー消失時の後始末は全部 GameUI の中）----
    m_GameUI.Update(m_Registry, m_Player, dt);

    // ---- マウスの捕獲（UI の開閉を見るので GameUI の後）----
    // UI（グリッド・三択・一時停止）が開いている間と死んだ後はマウスで操作するのでカーソルを出す
    m_Camera.UpdateMouseCapture(m_GameUI.IsModalOpen() || IsPlayerDead());

    // ---- 一時停止のメニューで選ばれた場面の切替 ----
    switch (m_GameUI.ConsumeMenuAction())
    {
    case PauseMenuUI::Action::Restart:
        Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::COLLISION_TEST);
        break;
    case PauseMenuUI::Action::Title:
        Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::TITLE);
        break;
    default:
        break;
    }

    // ---- 集約: グリッドが変わっていれば杖を組み直す ----
    // UI の直後に置く。編成した結果を同じフレームで反映させるため
    m_BackpackAggregate.Update(m_Registry);

    // ---- 場景光源: 点光源表は UpdateGameplay の中（CollectLights）で GPU へ上がるので、その前に積む。
    // 一時停止中も積む（その時は SceneBase::Render が上げる）
    m_Lighting.SubmitPointLights();
    m_Interaction.SubmitLights(m_Registry);   // 報酬の箱の目印（止まっている間も消さない）
    m_Pickups.SubmitLights();                 // 磁石の目印

    // TEMP-TEST: UI の自測は背包・一時停止を開くので（gameplay が止まる）、ここで回す
    if (m_AutoUI) UpdateAutoTestUI(dt);
    if (m_AutoChest) UpdateAutoTestChest(dt);       // 同上（背包を開いたまま）
    if (m_AutoBalance) UpdateAutoTestBalance(dt);   // 三択を自動で選ぶので同じくここ
    if (m_AutoPickup) UpdateAutoTestPickup(dt);     // 同上（4 択の画面を開いたまま撮る）

    if (!m_GameUI.ShouldPauseGame())
    {
        PROFILE_SCOPE("Gameplay");
        UpdateGameplay(dt);
    }

    // ---- 画面下の操作案内（近くに使える物がある時だけ）----
    m_GameUI.SetPrompt(m_Interaction.HasFocus() ? m_Interaction.GetPrompt() : nullptr);

    // ---- HUD：経過時間・撃破数と、画面外の目印 ----
    m_GameUI.SetRunInfo(m_RunTime, m_Swarm.GetCounters().killCount, m_Stage.stageTime);
    m_GameUI.SetBossBar(m_Stage.IsBossAlive() ? m_Stage.BossHpRatio() : -1.0f);
    UpdateHudMarkers();

    // ---- 死亡 → 倒れた姿を少し見せてからリザルトへ ----
    // 一時停止中でも進める（三択を開いたまま死ぬ事は無いが、止まると戻れない）
    // Boss を倒したら同じ流れで「ステージクリア」の幕 → リザルト（倒した後に死んでもクリア扱い）
    if (m_Stage.IsCleared())
    {
        m_DeathTimer += dt;
        m_GameUI.SetGameOver(m_DeathTimer, true);
        if (m_DeathTimer >= kDeathToResult) EndRun();
    }
    else if (IsPlayerDead())
    {
        m_DeathTimer += dt;
        m_GameUI.SetGameOver(m_DeathTimer);   // 「力尽きた」の幕
        if (m_DeathTimer >= kDeathToResult) EndRun();
    }
    else
    {
        m_GameUI.SetGameOver(-1.0f);
    }

    PROFILE_SCOPE("Debug panels (ImGui)");
    DrawDebugUI();
}

// ============================================================
// HUD の画面外の目印（報酬の箱 = 黄、精英 = 赤）
// ============================================================
void CollisionTestScene::UpdateHudMarkers()
{
    std::vector<HUDMarker> markers;
    m_Registry.CreateView<InteractableComponent>()
        .Each([&](Entity, InteractableComponent& it)
            {
                // 箱は黄、Boss の門は紫
                if (it.kind == InteractKind::BossPortal)
                    markers.push_back({ it.basePos + Vector3(0.0f, 2.0f, 0.0f), { 0.75f, 0.35f, 1.0f, 1.0f } });
                else
                    markers.push_back({ it.basePos + Vector3(0.0f, 0.6f, 0.0f), { 1.0f, 0.78f, 0.35f, 1.0f } });
            });
    // 磁石（赤）
    for (const Vector3& p : m_Pickups.GetPositions())
        markers.push_back({ p, { 1.0f, 0.25f, 0.20f, 1.0f } });
    // 面の Boss（GPU の回読の位置）
    if (m_Stage.IsBossAlive())
        markers.push_back({ m_Stage.BossPos() + Vector3(0.0f, 2.0f, 0.0f), { 0.85f, 0.25f, 1.0f, 1.0f } });
    // 計測用の的（無敵）も EliteTag を持つので外す
    m_Registry.CreateView<EliteTag, TransformComponent, HealthComponent>()
        .Each([&](Entity, EliteTag&, TransformComponent& tf, HealthComponent& hp)
            {
                if (hp.invincible || hp.current <= 0.0f) return;
                markers.push_back({ tf.position + Vector3(0.0f, 1.0f, 0.0f), { 1.0f, 0.30f, 0.30f, 1.0f } });
            });
    auto& cam = m_Camera.Camera();   // GetViewMatrix は const ではない
    m_GameUI.SetMarkers(cam.GetViewMatrix() * cam.GetProjectionMatrix(), std::move(markers));
}

// ============================================================
// 実際の gameplay 更新
// ============================================================
void CollisionTestScene::UpdateGameplay(float dt)
{
    if (m_AutoTest) UpdateAutoTest(dt);   // TEMP-TEST

    // ---- 遊んでいる時間（死んだら止める）----
    if (!IsPlayerDead())
        m_RunTime += dt;

    // ---- 負荷テスト（自動補充と小分けの生成）----
    m_Stress.Update(dt, m_Registry, m_Player, m_Swarm);

    // ============================================================
    // System の実行順（固定）
    // 操作 → 湧き依頼 → 衝突 → 物理 → 状態機 → 杖 → アニメ → マナ結算 → レベル判定
    //      → 箱 → 反応の特効 → カメラ → GPU gameplay Flush → 粒子 Flush
    //
    // ※雑魚の AI は GPU（SwarmEnemyAICS）。CPU には無い。
    //
    // ※状態機は物理の後。isGrounded / velocity が
    //   今フレームの最終値になっている必要があるため。
    //   物理より前に置くと接地判定が1フレーム古くなり、
    //   着地の見た目がずれる。
    //
    // ※能力値の注入（SetMoveSpeed 等）は行わない。
    //   PlayerControlSystem が PlayerStatsComponent を直接読む。
    // ============================================================
    {
        PROFILE_SCOPE("Player control + spawn");
        m_PlayerControlSystem.Update(m_Registry, dt, GetCamera());

        // ---- 湧き管理（雑魚は GPU。数えるのは GPU の存活数）----
        if (const Vector3* pp = PlayerPos())
        {
            m_Mobs.Update(m_Grid, *pp, dt, m_RunTime, m_Swarm);
            m_Stage.Update(m_Grid, *pp, m_RunTime, dt, m_Mobs, m_Swarm);   // 時間で起きる出来事（精英・最終波・Boss）
        }
    }
    {
        PROFILE_SCOPE("Collision (broadphase)");
        m_CollisionSystem.Update(m_Registry);
    }
    {
        PROFILE_SCOPE("Physics");
        m_PhysicsSystem.SetGravity(m_Gravity);
        m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);
    }

    static const char* const kRestSection = "State / weapon / anim / crates";   // GPU の Flush の前まで
    FrameProfiler::Get().Begin(kRestSection);
    m_PlayerStateSystem.Update(m_Registry, dt);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ---- 見た目のアニメ（杖の後: 「今フレーム撃った」を拾うため）----
    m_PlayerAnimSystem.Update(m_Registry, dt);
    m_SkinnedAnimSystem.Update(m_Registry, dt);

    // 草の風と踏み跡（物理の後の位置・接地で）
    if (const Vector3* pp = PlayerPos())
    {
        const bool grounded = m_Registry.Has<RigidbodyComponent>(m_Player) && m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        const bool sliding = m_Registry.Has<PlayerStateComponent>(m_Player) && m_Registry.Get<PlayerStateComponent>(m_Player).slideActive;
        m_Grass.Update(dt, *pp, grounded, sliding);
    }

    m_ManaSystem.Update(m_Registry, dt);

    // 投射物の生成・移動・命中・撃破報酬は全部 GPU（SwarmSystem）。
    // CPU 側にはもう無い。精英の弾を CPU に戻す時はここに書く

    m_Stress.RecordEmitterStats(m_ParticleSystem.GetPendingEmitterCount(),
        m_ParticleSystem.GetDroppedEmitterCount());

    // ============================================================
    // レベルアップの判定（候補の抽選まで）
    // ============================================================
    m_LevelUpSystem.Update(m_Registry);

    // ============================================================
    // 近くの物を使う（報酬の箱: 升級と同じ三択。レベルは上がらない）
    // レベル判定の後に置く: 同じフレームで升級が三択を出していたら、
    // 箱の三択は出せない（OfferChoices が false）→ 箱は消さずに残す
    // ============================================================
    {
        const bool blocked = DebugManager::Get().IsUsingDebugCamera()
            || ImGui::GetIO().WantCaptureKeyboard;
        const bool pressed = !blocked && !IsPlayerDead() && (InputMap::GetInteractTrigger() || m_AutoInteract);
        m_AutoInteract = false;   // TEMP-TEST

        const Entity used = m_Interaction.Update(m_Registry, m_Player, dt, pressed);
        Vector3 openedPos;
        if (m_Crates.TryOpen(m_Registry, used, m_Player, m_LevelUpSystem, m_Interaction, openedPos))
            m_Feedback.OnCrateOpened(openedPos);
        else if (const Vector3* pp = PlayerPos())
            m_Stage.TryUsePortal(m_Registry, used, m_Grid, *pp, m_Mobs, m_Swarm, m_Interaction);

        // 磁石（触れたら場の経験値オーブを全部吸い寄せる）
        if (const Vector3* pp = PlayerPos(); pp && !IsPlayerDead())
            m_Pickups.Update(m_Registry, m_Grid, *pp, dt, m_RunTime, m_Swarm);
    }

    // ---- 被弾と升級の反応（揺れ・斬撃・升級の光）----
    const float hpLost = TrackPlayerHpLoss();
    m_Camera.OnPlayerHit(hpLost);
    m_Feedback.Update(m_Registry, m_Player, dt, hpLost);
    m_Camera.OnAliveAreas(m_Swarm.GetCounters().aliveAreas);

    // ---- カメラ追従（最後）----
    m_Camera.Update(dt, PlayerPos());
    FrameProfiler::Get().End(kRestSection);

    // ============================================================
    // GPU 側 gameplay の Flush（粒子と同じ位置、粒子より前）
    //
    // 中で固定ステップを回す。渡すのは実 dt でよい。
    // 玩家の位置はここまでで確定しているので、この時点の値を上げる。
    //
    // 回読は 1〜2 フレーム古い。被弾は TryApplyHit へ流し、
    // 無敵時間の判定は向こうに任せる（CPU 側の接触ダメージと同じ窓口）
    // ============================================================
    if (m_Registry.IsValid(m_Player))
    {
        const auto& ptf = m_Registry.Get<TransformComponent>(m_Player);

        float playerRadius = 0.4f;
        if (m_Registry.Has<ColliderComponent>(m_Player))
            playerRadius = m_Registry.Get<ColliderComponent>(m_Player).radius;

        const bool playerAlive = !IsPlayerDead();

        // 玩家の体格は PlayerStats が持つ。接触判定用に毎フレーム GPU 側へ渡す
        if (m_Registry.Has<PlayerStatsComponent>(m_Player))
            m_Swarm.GetAIParams().playerCapsuleHalf =
            m_Registry.Get<PlayerStatsComponent>(m_Player).height * 0.5f;

        {
            PROFILE_SCOPE_GPU("Swarm Flush (GPU gameplay)");
            m_Swarm.Flush(ptf.position, playerRadius, playerAlive, dt, m_TotalTime);
        }

        // GPU 上で受けたダメージを CPU の玩家へ反映
        const float gpuDamage = m_Swarm.ConsumePlayerDamage();
        if (gpuDamage > 0.0f)
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, gpuDamage);
        // GPU 上で拾った経験値を CPU の玩家へ反映。
        // レベルアップの判定は LevelUpSystem（次フレーム頭）に任せて、ここは足すだけ
        const float gpuExp = m_Swarm.ConsumeExp();
        if (gpuExp > 0.0f) m_ExpGained += gpuExp;   // 戦績用
        if (gpuExp > 0.0f) m_Feedback.OnExpPicked(ptf.position);   // 拾った時のきらめき
        if (gpuExp > 0.0f && m_Registry.Has<LevelComponent>(m_Player))
            m_Registry.Get<LevelComponent>(m_Player).experience += gpuExp;
    }

    // ---- 死亡 → 燃焼消滅（HP が尽きた CPU 実体。消え終わったら MeshVFXSystem が破棄）----
    m_Elites.UpdateDeaths(m_Registry, m_MeshVFXSystem, m_VFXContext);
    m_MeshVFXSystem.Update(m_Registry, dt, m_VFXContext);

    // Mesh 発射の動作確認（Flush の前に積む）
    m_Stress.UpdateMeshEmitTest(dt, m_Registry, m_Player, m_ParticleSystem);

    // ============================================================
    // 粒子は1フレームに1回だけ Flush する。
    // Flush はコマンドを積むだけの処理なので、
    // 0 に近いままのはず。伸びるなら GPU を待っている。
    // ============================================================
    {
        PROFILE_SCOPE_GPU("Particles + lights");
        auto t0 = std::chrono::high_resolution_clock::now();

        // 範囲攻撃・反応の特効の見た目（emitter を積むので粒子の Flush より前）
        const Vector3* pp = PlayerPos();
        m_AreaVFX.Update(dt, pp ? *pp : Vector3::Zero);

        m_ParticleSystem.Flush(dt, m_TotalTime);

        // 点光源: CPU 側の VFX（範囲・精英）はもう積み終わっている。弾と範囲の分を GPU で追記
        m_Swarm.CollectLights();
        {   // TEMP-TEST crowd
            static int f = 0;
            if (f++ % 120 == 0) { Vector3 np, nv; float nd = 0; m_Swarm.GetNearestEnemy(np, nv, nd); std::cout << "[crowd] fps=" << Application::Get().GetTimer().GetFPS() << " alive=" << m_Swarm.GetCounters().aliveEnemies << " nearest=" << nd << " flushMs=" << m_Stress.GetFlushMsAvg() << std::endl; }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        m_Stress.RecordFlushMs(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    PROFILE_SCOPE("Gameplay debug draw");
    DrawGameplayDebug();
}

// ============================================================
// プレイ終了 → リザルトへ
// 戦績を g_LastRun に写してから切替を依頼する。2 回目以降は何もしない
// ============================================================
void CollisionTestScene::EndRun()
{
    if (m_RunEnded) return;
    m_RunEnded = true;

    g_LastRun.valid = true;
    g_LastRun.survivedSec = m_RunTime;   // 背包・三択で止めていた時間は含めない（HUD の表示と同じ）
    g_LastRun.level = (m_Registry.IsValid(m_Player) && m_Registry.Has<LevelComponent>(m_Player))
        ? m_Registry.Get<LevelComponent>(m_Player).level : 1;
    g_LastRun.kills = m_Swarm.GetCounters().killCount;   // 回読なので 1〜2 フレーム古い。許容
    g_LastRun.expGained = m_ExpGained;
    g_LastRun.cleared = m_Stage.IsCleared();

    Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::RESULT);
    std::cout << "[CollisionTestScene] run ended: " << (int)m_RunTime << "s, kills " << g_LastRun.kills << std::endl;
}

// ============================================================
// Render
// ============================================================
void CollisionTestScene::Render(Renderer& renderer)
{
    m_Lighting.Apply(renderer);
    auto& gfx = Application::Get().GetGraphics();
    ID3D11DeviceContext* ctx = gfx.GetContext();

    // ---- 0) 太陽の影図（段ごとに影を落とす物を深度だけで描く）→ 場面の HDR RT に戻す ----
    // 雑魚（GPU）は影図に入れない（足元の丸い影で代える）
    if (m_ShowMesh && GetCamera())
    {
        PROFILE_SCOPE_GPU("Shadow maps");
        m_Shadows.Render(ctx, renderer, *GetCamera(), m_Lighting.SunDirection(),
            [&](const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj, int cascade)
            {
                m_RenderSystem.RenderDepth(m_Registry, renderer, cascade == 0);
                m_StaticProps.RenderDepth(ctx, view, proj);
            });
        gfx.RestoreRenderTarget();
    }
    else
        m_Shadows.Disable(renderer);

    // 空（画面全体。深度を触らないので一番最初に）
    m_Lighting.DrawSky(ctx, GetCamera());

    SceneBase::Render(renderer);

    // ---- 1) モデル描画（CPU の実体 + GPU の雑魚）----
    if (m_ShowMesh)
    {
        { PROFILE_SCOPE_GPU("Models (ECS)"); m_RenderSystem.Render(m_Registry, renderer); }
        { PROFILE_SCOPE_GPU("Props (instanced)"); m_StaticProps.Render(renderer); }
        { PROFILE_SCOPE_GPU("Grass"); if (GetCamera()) m_Grass.Render(renderer, *GetCamera()); }   // 地形の後（深度で埋まる所を描かない）
        { PROFILE_SCOPE_GPU("Swarm draw"); m_Swarm.Render(GetCamera(), renderer.GetLightData()); }
    }
    if (m_ShowSwarmDebug)
        m_Swarm.RenderDebug(GetCamera());

    {
        PROFILE_SCOPE_GPU("Billboards + sprites");
        // ---- 2) ビルボード（投射物とオーブの芯）----
        if (m_ShowBillboard)
            m_ProjectileRenderer.Render(m_Registry, GetCamera());

        // ---- 2b) 連番絵（CPU の Sprite entry と、GPU の範囲が出した物）。粒子の前 ----
        m_SpriteRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera());
        m_BeamRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera());   // 光線（加算）
        m_Swarm.RenderSprites(GetCamera());
    }

    // ---- 3) 粒子（VFX 本体）----
    if (m_ShowParticle)
    {
        PROFILE_SCOPE_GPU("Particles draw");
        m_ParticleSystem.SetCamera(GetCamera());
        m_ParticleSystem.SetLight(renderer.GetLightData());   // 立方体粒子の Lambert 用
        m_ParticleSystem.Render();
    }
    m_Shadows.Unbind(ctx);   // 次のフレームで影図を DSV にするので外す

    // ============================================================
    // 4) UI（一番手前。Begin/End の管理は GameUI の中）
    // ============================================================
    PROFILE_SCOPE_GPU("Game UI");
    m_GameUI.Render(m_Registry, m_Player);
}
