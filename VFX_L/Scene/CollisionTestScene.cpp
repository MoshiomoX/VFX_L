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
#include "Graphics/Light/PointLightManager.h"
#include "UI/UIDeco.h"
#include <algorithm>
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

    // DebugManager の y = 0 の参照格子は消す（地面と同じ高さで深度が競り合い、地面に 1m の縞が出ていた。
    // 9-30 から「地面に格子線」と記録していた物の正体）。Shutdown で元に戻す（編集器の場面は使う）
    m_PrevDebugGrid = DebugManager::Get().GetShowGrid();
    DebugManager::Get().SetShowGrid(false);

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
        m_AutoLayers = m_AutoTest && strcmp(env, "layers") == 0;         // 値が layers なら山頂・平原・鉱洞の三層
        m_AutoPortal = m_AutoTest && strcmp(env, "portal") == 0;         // 値が portal なら Boss の門の石の拱と渦
        m_AutoEdgeRock = m_AutoTest && strcmp(env, "edgerock") == 0;     // 値が edgerock なら外周の岩の衝突
        m_AutoBossExit = m_AutoTest && strcmp(env, "bossexit") == 0;     // 値が bossexit なら Boss が洞から出られるか
        m_AutoSoak = m_AutoTest && strcmp(env, "soak") == 0;             // 値が soak なら実時間の通し検査（穿模・寻路）
        m_AutoMusic = m_AutoTest && strcmp(env, "music") == 0;           // 値が music なら BGM の全曲・継ぎ目・切り替え
        m_AutoArrow = m_AutoTest && strcmp(env, "arrow") == 0;           // 値が arrow なら黄金の矢を横から撮る
        m_AutoChain = m_AutoTest && strcmp(env, "chain") == 0;           // 値が chain なら火球 + 石弾 → 隕石の誘発
        m_AutoChest = m_AutoTest && strcmp(env, "chest") == 0;           // 値が chest なら魔法書の木箱の物理
        m_AutoBeam = m_AutoTest && strcmp(env, "beam") == 0;             // 値が beam なら追尾弾 + 弧 → 魔導光線
        m_AutoStuck = m_AutoTest && strcmp(env, "stuck") == 0;           // 値が stuck なら雑魚の壁詰まり
        m_AutoGhost = m_AutoTest && strcmp(env, "ghost") == 0;           // 値が ghost なら最終波の幽霊
        m_AutoClip = m_AutoTest && strcmp(env, "clip") == 0;             // 値が clip なら障害物の横で雑魚の食い込みを測る
        m_AutoKnock = m_AutoTest && strcmp(env, "knock") == 0;           // 値が knock なら被弾のノックバックを測る
        m_AutoDrop = m_AutoTest && strcmp(env, "drop") == 0;             // 値が drop なら台地からの飛び降り
        m_AutoBeamTrack = m_AutoTest && strcmp(env, "beamtrack") == 0;   // 値が beamtrack なら光線の標的乗り換え
        m_AutoSurge = m_AutoTest && strcmp(env, "surge") == 0;           // 値が surge なら魔力解放（Q）
        m_AutoPoison = m_AutoTest && strcmp(env, "poison") == 0;         // 値が poison なら毒（池の減速と持続ダメージ）
        m_AutoDeath = m_AutoTest && strcmp(env, "death") == 0;           // 値が death なら死んだ敵の砕け散り
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
    if (!m_LiquidRenderer.Initialize(device))
        std::cout << "[Error] VFXLiquidRenderer init failed" << std::endl;
    m_VFXContext.liquidRenderer = &m_LiquidRenderer;

    // ---------- 各 System が使う VFX の登録 ----------
    // 投射物プロファイル（弾の飛び方・見た目）を先に読む（GPU の表は下でもう一度 Build）
    ProjectileProfileDB::LoadAll();

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

    // ---------- 面（草原 / 砂漠 / 遺跡）----------
    // 前の面から引き継いだ番号（g_RunCarry.stage。タイトルから来た時は 1）。VFXL_STAGE=n で上書き（調試）
    m_StageIndex = g_RunCarry.stage;
    {
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_STAGE", env, sizeof(env)) > 0)
            m_StageIndex = atoi(env);
        m_StageIndex = std::clamp(m_StageIndex, 1, StageConfig::kStageCount);
    }
    const StageDef& stageDef = StageConfig::Get(m_StageIndex);
    m_TerrainConfig.biome = stageDef.biome;
    m_Lighting.ApplyPreset(stageDef.light);
    std::cout << "[CollisionTestScene] stage " << m_StageIndex << " biome " << (int)stageDef.biome << std::endl;
    if (m_AutoTest)
    {
        char line[48];
        snprintf(line, sizeof(line), "stage %d biome %d", m_StageIndex, (int)stageDef.biome);
        AutoTestLog(line);   // TEMP-TEST: 面の引き継ぎの確認用
    }

    // ---------- 地形（格子对齐。台地と坂道の野原）----------
    // 場地: 100 x 100 マス = 200m x 200m（山頂・平原・鉱洞の三層。2026-10-02 に一度 300m にしたが、
    // 10-03 用户「大きすぎる」で 200m へ戻した。高低差はそのまま、水平だけ 2/3）。
    // 開局ごとに seed を変える（同じ seed なら同じ地形。VFXL_TERRAIN_SEED で固定できる）
    m_Grid.Init(100, 100);
    {
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_TERRAIN_SEED", env, sizeof(env)) > 0)
            m_TerrainConfig.seed = (uint32_t)strtoul(env, nullptr, 10);
        else
            m_TerrainConfig.seed = std::random_device{}() % 100000u;
        // TEMP-TEST: 外周の岩を縁から内に入れない = 岩の衝突 0 個（衝突の有無で fps を比べる用）
        if (GetEnvironmentVariableA("VFXL_NO_EDGE_COLLIDERS", env, sizeof(env)) > 0)
        {
            m_TerrainConfig.edgeRockIntrudeMin = -1.0f;
            m_TerrainConfig.edgeRockIntrudeMax = 0.0f;
        }
    }
    std::vector<uint8_t> grassMask;
    m_Torches.clear();
    TerrainGenerator::Generate(m_Registry, device, m_Grid, m_TerrainConfig, m_Terrain, &grassMask, &m_Torches,
        &m_TerrainLayout);
    BlockUnreachablePockets();
    // 置物（木・岩・茂み）はモデル毎の instanced 描画へ
    if (!m_StaticProps.Initialize(device))
        std::cout << "[Error] StaticPropRenderer init failed" << std::endl;
    m_StaticProps.Build(m_Registry);
    // 草（GPU で生やす葉）
    if (!m_Grass.Initialize(device, context))
        std::cout << "[Error] GrassRenderer init failed" << std::endl;
    // 面ごとの草（砂漠は疎らな枯れ草、遺跡は無し）。色は床の色（GroundColor）に掛かる
    // Initialize が VFXL_NO_GRASS で切った時はそのまま（面の設定で上書きしない）
    m_Grass.GetSettings().enabled = m_Grass.GetSettings().enabled && stageDef.grass;
    m_Grass.GetSettings().spacing = stageDef.grassSpacing;
    m_Grass.GetSettings().rootColor = stageDef.grassRoot;
    m_Grass.GetSettings().tipColor = stageDef.grassTip;
    m_Grass.GetSettings().heightMin = stageDef.grassHeightMin;
    m_Grass.GetSettings().heightMax = stageDef.grassHeightMax;
    m_Grass.Build(m_Grid, grassMask, m_TerrainConfig.seed, m_TerrainConfig.biome);

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
    m_Audio.Init(m_Swarm, m_StageIndex);   // 範囲の音の表（範囲の profile と VFX 表の後）
    m_Mobs.statMulBonus = stageDef.difficultyBonus;   // 面の難度の下駄
    m_WeaponSystem.SetSwarm(&m_Swarm);

    // ============================================================
    // プレイヤー
    // 組み立ては PlayerFactory に任せる。
    // シーンはどの Component が付いているかを知らなくてよい。
    // ============================================================
    PlayerFactory::Config pcfg;
    pcfg.color = { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f };

    m_Player = PlayerFactory::Create(m_Registry, device, pcfg);
    ApplyRunCarry();   // 前の面の背包・魔法書・等級・能力値（あれば）

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
// Shutdown
// ============================================================
void CollisionTestScene::Shutdown()
{
    m_Audio.StopMusic();   // 次の場面（タイトル・結果）が自分の曲を流す
    SceneLighting::ClearFog(Application::Get().GetRenderer());   // Renderer は他の場面と共有
    m_Shadows.Disable(Application::Get().GetRenderer());         // 同上（影を切らないと他の場面が真っ暗）
    m_Shadows.Unbind(Application::Get().GetGraphics().GetContext());
    m_Shadows.Shutdown();
    m_Swarm.Shutdown();
    m_StaticProps.Shutdown();
    m_Grass.Shutdown();
    m_GameUI.Shutdown();
    DebugManager::Get().SetShowGrid(m_PrevDebugGrid);   // 参照格子を元に戻す（DebugManager も場面間で共有）
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
    // 三層の場地では山頂・鉱洞にも箱を置く（登る・潜るご褒美）
    m_Crates.Spawn(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed, m_Interaction,
        &m_TerrainLayout.summitCells, &m_TerrainLayout.mineCells);
    // Boss を呼ぶ門・磁石も同じ時に置き直す（地形が変わると前の場所は歩けないかもしれない）。
    // 門は鉱洞の一番奥（無ければ従来通り玩家の周り）。面は鉱洞の真ん中の方（洞の中から近づく）
    Vector3 mineMid;
    for (int i : m_TerrainLayout.mineCells) mineMid += m_Grid.CellToWorld(i % m_Grid.Width(), i / m_Grid.Width());
    const bool hasMine = !m_TerrainLayout.mineCells.empty();
    if (hasMine) mineMid /= (float)m_TerrainLayout.mineCells.size();
    m_Stage.SpawnPortal(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed, m_Interaction,
        m_TerrainLayout.hasMineDeep ? &m_TerrainLayout.mineDeep : nullptr, hasMine ? &mineMid : nullptr);
    // 石の拱の間の渦（粒子。門の向きに回す）。Boss を呼ぶと UpdateGameplay が止める
    if (m_PortalVfx) m_AreaVFX.StopInstance(m_PortalVfx);
    m_PortalVfx = 0;
    if (m_Registry.IsValid(m_Stage.GetPortal()))
    {
        m_PortalVfx = m_AreaVFX.Play("BossPortal.json", m_Stage.GetPortalCenter(), 1.0e9f, false, m_VFXContext);
        if (m_PortalVfx) m_AreaVFX.RotateInstance(m_PortalVfx, m_Stage.GetPortalYaw());
    }
    m_Pickups.Reset(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed);
    BlockPropCells();
}

// ============================================================
// 置物の下のマスを雑魚用に塞ぐ（2026-10-03、通し検査 soak で見つけた）。
// 報酬の箱・Boss の門の柱の衝突は玩家にしか効かず、GPU の雑魚は格子しか見ないので素通りしていた。
// 木・岩と同じく、衝突の箱が掛かるマスを塞ぐ（元から塞がっていたマスは覚えない = 戻さない）
// ============================================================
void CollisionTestScene::BlockPropCells()
{
    const int W = m_Grid.Width();
    for (const auto& b : m_PropBlocks)
        for (int c : b.cells) m_Grid.SetWalkable(c % W, c / W, true);
    m_PropBlocks.clear();

    auto block = [&](Entity e, bool crate)
        {
            if (!m_Registry.IsValid(e) || !m_Registry.Has<ColliderComponent>(e) || !m_Registry.Has<TransformComponent>(e))
                return;
            const auto& col = m_Registry.Get<ColliderComponent>(e);
            const Vector3 c = m_Registry.Get<TransformComponent>(e).position + col.offset;
            const Vector3 h = col.halfExtents;
            int x0, z0, x1, z1;
            m_Grid.WorldToCell(c - h, x0, z0);
            m_Grid.WorldToCell(c + h, x1, z1);
            PropBlock b{ e, crate, {} };
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    if (m_Grid.IsWalkable(x, z))
                    {
                        m_Grid.SetWalkable(x, z, false);
                        b.cells.push_back(z * W + x);
                    }
            m_PropBlocks.push_back(std::move(b));
        };
    for (Entity e : m_Crates.GetCrates()) block(e, true);
    for (Entity e : m_Stage.GetPortalPillars()) block(e, false);
    m_Swarm.RefreshWalkable(m_Grid);
}

// ============================================================
// 出られない袋小路を塞ぐ（2026-10-03、通し検査 soak で見つけた）。
// 山頂の凹み・崖と木 / 台地に囲まれた所など、場地の真ん中（開局の場所）へ歩いて行けない歩けるマスがあると、
// そこに湧いた雑魚は流場の答えが無く、何分でもその場に留まっていた（3 面 × 10 分で 54 件）。
// 雑魚と同じ規則（FlowField：崖は上れない、下りは飛び降り）で真ん中を目標に解き、届かないマスを塞ぐ
// （湧かない・入らない）。地形を作った直後、GPU へ上げる前に呼ぶ
// ============================================================
void CollisionTestScene::BlockUnreachablePockets()
{
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    if (W <= 0 || D <= 0) return;
    std::vector<uint8_t> walk((size_t)W * D);
    std::vector<float> hgt((size_t)W * D);
    for (int z = 0; z < D; ++z)
        for (int x = 0; x < W; ++x)
        {
            walk[(size_t)z * W + x] = m_Grid.IsWalkable(x, z) ? 1 : 0;
            const Vector3 c = m_Grid.CellToWorld(x, z);
            hgt[(size_t)z * W + x] = m_Grid.SampleHeight(c.x, c.z);
        }
    FlowField ff;
    ff.SetGrid(W, D, walk, hgt, m_Grid.Heights(), GridWorld::kHeightSub);
    if (!ff.Build(W / 2, D / 2)) return;
    const auto& cost = ff.Costs();
    int blocked = 0;
    for (size_t i = 0; i < cost.size() && i < walk.size(); ++i)
        if (walk[i] && cost[i] > 1e30f)
        {
            m_Grid.BlockArea((int)(i % W), (int)(i / W), 1, 1);
            ++blocked;
        }
    std::cout << "[Terrain] unreachable pockets blocked: " << blocked << " cells" << std::endl;
}

void CollisionTestScene::UpdatePropBlocks()
{
    const int W = m_Grid.Width();
    bool changed = false;
    for (auto it = m_PropBlocks.begin(); it != m_PropBlocks.end();)
    {
        const bool gone = !m_Registry.IsValid(it->entity)
            || (it->crate && !m_Registry.Has<InteractableComponent>(it->entity));
        if (!gone) { ++it; continue; }
        for (int c : it->cells) m_Grid.SetWalkable(c % W, c / W, true);
        it = m_PropBlocks.erase(it);
        changed = true;
    }
    if (changed) m_Swarm.RefreshWalkable(m_Grid);
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
    SubmitTorchLights();                      // 遺跡の松明（玩家に近い物だけ）
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

        // 音：GPU の範囲（命中・爆発・撃破）、玩家の跳び / 着地 / 滑り、BGM の切り替え、一回物
        BattleAudio::State as;
        as.bossAlive = m_Stage.IsBossAlive();
        as.finalWave = m_Stage.stageTime > 0.0f && m_RunTime >= m_Stage.stageTime;
        as.playerDead = IsPlayerDead();
        as.cleared = m_Stage.IsCleared();
        m_Audio.Update(dt, m_Swarm, m_Registry, m_Player, as);
    }

    // ---- 画面下の操作案内（近くに使える物がある時だけ）----
    m_GameUI.SetPrompt(m_Interaction.HasFocus() ? m_Interaction.GetPrompt() : nullptr);

    // ---- HUD：経過時間・撃破数と、画面外の目印 ----
    m_GameUI.SetRunInfo(m_RunTime, m_Swarm.GetCounters().killCount, m_Stage.stageTime);
    m_GameUI.SetStage(m_StageIndex, StageConfig::Get(m_StageIndex).name);
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
        UpdatePropBlocks();   // 開けた箱の下のマスを雑魚に戻す

        // 門が使われた（Boss を呼んだ。面板の「Summon Boss Now」も含めて、門から使える印が消えた）ら渦を止める
        const Entity portal = m_Stage.GetPortal();
        if (m_PortalVfx && (!m_Registry.IsValid(portal) || !m_Registry.Has<InteractableComponent>(portal)))
        {
            m_AreaVFX.StopInstance(m_PortalVfx);
            m_PortalVfx = 0;
        }

        // 磁石（触れたら場の経験値オーブを全部吸い寄せる）
        if (const Vector3* pp = PlayerPos(); pp && !IsPlayerDead())
            m_Pickups.Update(m_Registry, m_Grid, *pp, dt, m_RunTime, m_Swarm);
    }

    // ---- 被弾と升級の反応（揺れ・斬撃・升級の光）----
    const float hpLost = TrackPlayerHpLoss();
    m_Camera.OnPlayerHit(hpLost);
    m_Feedback.Update(m_Registry, m_Player, dt, hpLost);
    m_Camera.OnShakeAreas(m_Swarm.ConsumeShakeAreas());   // 爆発・光線だけ（命中の火花・土煙では揺らさない）

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
        // ノックバック（打撃の向きは別の回読。ダメージと前後して届くことがある）。
        // 無敵中は下がらない。ただし無敵が始まったばかり（この打撃のダメージが先に届いた）なら下がる。
        // 同じフレームに爆発があれば爆発の方（遠く・少し浮く）
        const auto hits = m_Swarm.ConsumePlayerHits();
        if ((hits.melee > 0 || hits.blasts > 0) && playerAlive && m_Registry.Has<PlayerStateComponent>(m_Player))
        {
            const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            const bool fresh = !st.IsInvincible() || st.invincibleTimer > st.invincibleAfterHit - 0.2f;
            if (fresh)
            {
                if (hits.blasts > 0) PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, hits.blastDir, true);
                else                 PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, hits.meleeDir, false);
            }
        }
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
    g_LastRun.stage = m_StageIndex;
    g_LastRun.hasNextStage = g_LastRun.cleared && m_StageIndex < StageConfig::kStageCount;

    // ---- 次の面への引き継ぎ（クリアした時だけ。力尽きたら最初から）----
    g_RunCarry.Reset();
    if (g_LastRun.hasNextStage && m_Registry.IsValid(m_Player))
    {
        g_RunCarry.stage = m_StageIndex + 1;
        g_RunCarry.hasPlayer = true;
        if (m_Registry.Has<BackpackComponent>(m_Player))    g_RunCarry.backpack = m_Registry.Get<BackpackComponent>(m_Player);
        if (m_Registry.Has<SpellbookComponent>(m_Player))   g_RunCarry.spellbook = m_Registry.Get<SpellbookComponent>(m_Player);
        if (m_Registry.Has<LevelComponent>(m_Player))       g_RunCarry.level = m_Registry.Get<LevelComponent>(m_Player);
        if (m_Registry.Has<ManaComponent>(m_Player))        g_RunCarry.mana = m_Registry.Get<ManaComponent>(m_Player);
        if (m_Registry.Has<HealthComponent>(m_Player))      g_RunCarry.health = m_Registry.Get<HealthComponent>(m_Player);
        if (m_Registry.Has<PlayerStatsComponent>(m_Player)) g_RunCarry.stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
        g_RunCarry.killsBefore = g_LastRun.kills;
    }

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
        // ---- 2a) CPU で出した液溜まり（地面なので連番絵より先。高さ場は群れの物を借りる）----
        m_LiquidRenderer.SetTerrain(m_Swarm.GetHeightSRV(), m_Swarm.GetFrameCB());
        m_LiquidRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera(), renderer.GetLightData());
        // ---- 2) 連番絵（CPU の Sprite entry と、GPU の範囲が出した物）。粒子の前 ----
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

// ============================================================
// 遺跡の松明・鉱洞の壁の松明：玩家に近い順に StageDef::torchLights 個だけ点光源を付ける
// （松明は外周に 60 本ほど。全部付けると点光源の上限 64 を使い切る）。
// 50m より遠い物は付けない（草原・砂漠は鉱洞の中にしか無く、遠くで光っても見えない）
// ============================================================
void CollisionTestScene::SubmitTorchLights()
{
    if (m_Torches.empty() || !m_Registry.IsValid(m_Player)) return;
    const StageDef& def = StageConfig::Get(m_StageIndex);
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;

    // 近い順に並べる（松明の数は少ないので毎フレーム並べ替えても軽い）
    std::vector<std::pair<float, size_t>> order;
    order.reserve(m_Torches.size());
    for (size_t i = 0; i < m_Torches.size(); ++i)
        order.push_back({ (m_Torches[i] - pp).LengthSquared(), i });
    std::sort(order.begin(), order.end());

    auto& lights = PointLightManager::Get();
    const float t = UIDeco::Clock();
    const int n = (std::min)((int)order.size(), def.torchLights);
    constexpr float kMaxDistSq = 50.0f * 50.0f;
    for (int k = 0; k < n && order[k].first < kMaxDistSq; ++k)
    {
        const Vector3& p = m_Torches[order[k].second];
        // 松明ごとに位相をずらした揺らぎ
        const float flicker = 0.85f + 0.15f * std::sin(t * 9.0f + (float)order[k].second * 1.7f)
            * std::cos(t * 5.3f + (float)order[k].second * 0.9f);
        // 鉱洞の中（平原より低い）は小さく：点光源は壁を抜けるので、大きいと洞の外の壁・草まで照らす
        const bool inCave = p.y < -1.0f;
        lights.Add(p, { 1.0f, 0.62f, 0.25f }, inCave ? 10.0f : 18.0f, (inCave ? 2.2f : 2.6f) * flicker);
    }
}

// ============================================================
// 前の面から引き継いだ玩家（背包・魔法書・等級・能力値）。HP / MP は上限から始める
// ============================================================
void CollisionTestScene::ApplyRunCarry()
{
    if (!g_RunCarry.hasPlayer || !m_Registry.IsValid(m_Player)) return;
    g_RunCarry.hasPlayer = false;   // 一度だけ（F5 の再読込で二重に効かないように）

    if (m_Registry.Has<BackpackComponent>(m_Player))
    {
        m_Registry.Get<BackpackComponent>(m_Player) = g_RunCarry.backpack;
        m_Registry.Get<BackpackComponent>(m_Player).dirty = true;
    }
    if (m_Registry.Has<SpellbookComponent>(m_Player))
        m_Registry.Get<SpellbookComponent>(m_Player) = g_RunCarry.spellbook;
    if (m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        lv = g_RunCarry.level;
        lv.pendingChoices.clear();
    }
    if (m_Registry.Has<PlayerStatsComponent>(m_Player))
        m_Registry.Get<PlayerStatsComponent>(m_Player) = g_RunCarry.stats;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = g_RunCarry.mana.max;
        mp.regen = g_RunCarry.mana.regen;
        mp.current = mp.max;
        mp.pendingSpend = 0.0f;
    }
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        hp.max = g_RunCarry.health.max;
        hp.current = hp.max;
    }
    std::cout << "[CollisionTestScene] carried the player over from the previous stage (level "
        << g_RunCarry.level.level << ")" << std::endl;
    if (m_AutoTest)
    {
        char line[96];
        snprintf(line, sizeof(line), "carry level %d items %d book %d", g_RunCarry.level.level,
            (int)g_RunCarry.backpack.items.size(), (int)g_RunCarry.spellbook.entries.size());
        AutoTestLog(line);   // TEMP-TEST: 面の引き継ぎの確認用
    }
}
