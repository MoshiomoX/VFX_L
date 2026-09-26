// ============================================================
// CollisionTestScene.cpp
// 初期化・毎フレームの流れ・描画・結算。
// ImGui の面板とデバッグ描画は CollisionTestSceneDebug.cpp
// ============================================================
#include "Scene/CollisionTestScene.h"

#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
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
#include "Manager/InputMap.h"
#include "Manager/ResourceManager.h"
#include "Core/Application.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <chrono>
#include <iostream>

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
        char env[8] = {};   // TEMP-TEST
        m_AutoTest = GetEnvironmentVariableA("VFXL_BATTLE_AUTOTEST", env, sizeof(env)) > 0;
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

    // ---------- 地形（格子对齐）----------
    // 場地: 100 x 100 マス = 200m x 200m（旧場地 24m の約8倍幅）。
    // 障害物の配置は seed で再現できる
    m_Grid.Init(100, 100);

    TerrainGenerator::Config tcfg;
    tcfg.seed = m_TerrainSeed;
    tcfg.obstacleCount = 40;
    TerrainGenerator::Generate(m_Registry, device, m_Grid, tcfg, m_Terrain);

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
    pcfg.maxHealth = 1000000.0f;   // TEMP-TEST

    m_Player = PlayerFactory::Create(m_Registry, device, pcfg);

    // ---------- 反応の特効（升級・開箱・被弾）----------
    m_Feedback.Init(&m_AreaVFX, &m_VFXContext);
    if (m_AutoTest)
        m_Feedback.onPlayed = [this](const char* file) { AutoTestLog(file); };   // TEMP-TEST

    // ---------- 報酬の箱（玩家の周りに固定数。玩家の位置が要るので最後）----------
    m_Crates.Init();
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
    m_Swarm.Shutdown();
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
    m_Crates.Spawn(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainSeed, m_Interaction);
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

    if (!m_GameUI.ShouldPauseGame())
        UpdateGameplay(dt);

    // ---- 画面下の操作案内（近くに使える物がある時だけ）----
    m_GameUI.SetPrompt(m_Interaction.HasFocus() ? m_Interaction.GetPrompt() : nullptr);

    // ---- HUD：経過時間・撃破数と、画面外の目印 ----
    m_GameUI.SetRunInfo(m_RunTime, m_Swarm.GetCounters().killCount);
    UpdateHudMarkers();

    // ---- 死亡 → 倒れた姿を少し見せてからリザルトへ ----
    // 一時停止中でも進める（三択を開いたまま死ぬ事は無いが、止まると戻れない）
    if (IsPlayerDead())
    {
        m_DeathTimer += dt;
        m_GameUI.SetGameOver(m_DeathTimer);   // 「力尽きた」の幕
        if (m_DeathTimer >= kDeathToResult) EndRun();
    }
    else
    {
        m_GameUI.SetGameOver(-1.0f);
    }

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
                markers.push_back({ it.basePos + Vector3(0.0f, 0.6f, 0.0f), { 1.0f, 0.78f, 0.35f, 1.0f } });
            });
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
    m_PlayerControlSystem.Update(m_Registry, dt, GetCamera());

    // ---- 湧き管理（雑魚は GPU。数えるのは GPU の存活数）----
    if (const Vector3* pp = PlayerPos())
        m_Mobs.Update(m_Grid, *pp, dt, m_Swarm);

    m_CollisionSystem.Update(m_Registry);

    m_PhysicsSystem.SetGravity(m_Gravity);
    m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);

    m_PlayerStateSystem.Update(m_Registry, dt);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ---- 見た目のアニメ（杖の後: 「今フレーム撃った」を拾うため）----
    m_PlayerAnimSystem.Update(m_Registry, dt);
    m_SkinnedAnimSystem.Update(m_Registry, dt);

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
    }

    // ---- 被弾と升級の反応（揺れ・斬撃・升級の光）----
    const float hpLost = TrackPlayerHpLoss();
    m_Camera.OnPlayerHit(hpLost);
    m_Feedback.Update(m_Registry, m_Player, dt, hpLost);
    m_Camera.OnAliveAreas(m_Swarm.GetCounters().aliveAreas);

    // ---- カメラ追従（最後）----
    m_Camera.Update(dt, PlayerPos());

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

        m_Swarm.Flush(ptf.position, playerRadius, playerAlive, dt, m_TotalTime);

        // GPU 上で受けたダメージを CPU の玩家へ反映
        const float gpuDamage = m_Swarm.ConsumePlayerDamage();
        if (gpuDamage > 0.0f)
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, gpuDamage);
        // GPU 上で拾った経験値を CPU の玩家へ反映。
        // レベルアップの判定は LevelUpSystem（次フレーム頭）に任せて、ここは足すだけ
        const float gpuExp = m_Swarm.ConsumeExp();
        if (gpuExp > 0.0f) m_ExpGained += gpuExp;   // 戦績用
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

    Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::RESULT);
    std::cout << "[CollisionTestScene] run ended: " << (int)m_RunTime << "s, kills " << g_LastRun.kills << std::endl;
}

// ============================================================
// Render
// ============================================================
void CollisionTestScene::Render(Renderer& renderer)
{
    m_Lighting.Apply(renderer);

    SceneBase::Render(renderer);

    // ---- 1) モデル描画（CPU の実体 + GPU の雑魚）----
    if (m_ShowMesh)
    {
        m_RenderSystem.Render(m_Registry, renderer);
        m_Swarm.Render(GetCamera(), renderer.GetLightData());
    }
    if (m_ShowSwarmDebug)
        m_Swarm.RenderDebug(GetCamera());

    // ---- 2) ビルボード（投射物とオーブの芯）----
    if (m_ShowBillboard)
        m_ProjectileRenderer.Render(m_Registry, GetCamera());

    // ---- 2b) 連番絵（CPU の Sprite entry と、GPU の範囲が出した物）。粒子の前 ----
    m_SpriteRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera());
    m_Swarm.RenderSprites(GetCamera());

    // ---- 3) 粒子（VFX 本体）----
    if (m_ShowParticle)
    {
        m_ParticleSystem.SetCamera(GetCamera());
        m_ParticleSystem.SetLight(renderer.GetLightData());   // 立方体粒子の Lambert 用
        m_ParticleSystem.Render();
    }

    // ============================================================
    // 4) UI（一番手前。Begin/End の管理は GameUI の中）
    // ============================================================
    m_GameUI.Render(m_Registry, m_Player);
}
