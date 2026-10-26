// ============================================================
// CollisionTestSceneInit.cpp
// CollisionTestScene::Init の各段（2026-10-25 に CollisionTestScene.cpp から分けた。中身はそのまま、Init が上から順に呼ぶ）
// と、前の面からの引き継ぎ（ApplyRunCarry）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Component/HealthComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerFactory.h"
#include "Player/LevelComponent.h"
#include "Player/WalletComponent.h"
#include "VFX_Editor/VFXId.h"
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"
#include "Scene/RunResult.h"
#include "Core/GameSettings.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

// ============================================================
// 描画まわり：粒子・VFX の各描画・UI・エリートの的・アウトライン・影
// ============================================================
void CollisionTestScene::InitRenderers(ID3D11Device* device, ID3D11DeviceContext* context)
{
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
    if (!m_MeshRenderer.Initialize(device))
        std::cout << "[Error] VFXMeshRenderer init failed" << std::endl;
    m_VFXContext.meshRenderer = &m_MeshRenderer;
    if (!m_LiquidRenderer.Initialize(device))
        std::cout << "[Error] VFXLiquidRenderer init failed" << std::endl;
    m_VFXContext.liquidRenderer = &m_LiquidRenderer;

    // ---------- 各 System が使う VFX の登録 ----------
    // 投射物プロファイル（弾の飛び方・見た目）を先に読む（GPU の表は下でもう一度 Build）
    ProjectileProfileDB::LoadAll();

    // 燃焼消滅（Mesh 発射 + 溶解の縁）。アイテムではないので VFXDatabase から直接引く
    if (const char* path = VFXDatabase::GetPath(VFXId::DeathBurn))
        m_MeshVFXSystem.RegisterVFX(VFXId::DeathBurn, path);

    // ---------- UI ----------
    // ※ItemDatabase::Initialize の後（LoadIcons が定義を読む）
    if (!m_GameUI.Initialize(device, context, m_ScreenW, m_ScreenH))
        std::cout << "[Error] GameUI init failed" << std::endl;
    m_GameUI.SetLevelUpSystem(&m_LevelUpSystem);   // 四択の金貨での引き直し

    // ---------- エリートの的（骨付きモデルが読めない時のカプセルを用意）----------
    // 雑魚のモデルは SwarmSystem が自前で持つ。ここには置かない
    m_Elites.Init(device);
    m_Lighting.Init(device);   // 空のシェーダー
    m_Outline.Initialize(device);   // トゥーンのアウトライン
    {
        char env[8] = {};   // TEMP-TEST: VFXL_NO_OUTLINE=1 でアウトラインを切る（見比べ用）
        if (GetEnvironmentVariableA("VFXL_NO_OUTLINE", env, sizeof(env)) > 0 && env[0] != '0')
            m_Outline.GetParams().enabled = false;
        if (GetEnvironmentVariableA("VFXL_OUTLINE_DEBUG", env, sizeof(env)) > 0 && env[0] != '0')
            m_Outline.GetParams().debugView = true;   // 目からの距離を縞で
    }
    if (!m_Shadows.Initialize(device))
        std::cout << "[Error] ShadowMap init failed" << std::endl;
}

// ============================================================
// 何面目か（引き継ぎ / 環境変数 / タイトル・選択）、Tier、面の照明
// ============================================================
void CollisionTestScene::InitStage()
{
    // ---------- 面（草原 / 砂漠 / 遺跡）----------
    // マップ（g_RunCarry.stage。ステージ選択で決める、プレイの間ずっと同じ）と何面目か（g_RunCarry.pos）、Tier。
    // VFXL_STAGE=n で上書き（デバッグ：そのマップ。VFXL_TIER=n で Tier、VFXL_POS=n で何面目か = 2 面目・3 面目の難度を見る）
    m_StageIndex = g_RunCarry.stage;
    m_RunPos = g_RunCarry.pos;
    m_RunTier = g_RunCarry.tier;
    {
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_STAGE", env, sizeof(env)) > 0)
        {
            m_StageIndex = atoi(env);
            m_RunPos = 1;
            m_RunTier = 1;
            if (GetEnvironmentVariableA("VFXL_TIER", env, sizeof(env)) > 0) m_RunTier = atoi(env);
            if (GetEnvironmentVariableA("VFXL_POS", env, sizeof(env)) > 0) m_RunPos = atoi(env);
        }
        m_StageIndex = std::clamp(m_StageIndex, 1, StageConfig::kStageCount);
        if (m_TitleScene) m_StageIndex = 1;   // タイトルの景色はいつも草原
        if (m_SelectScene) m_StageIndex = std::clamp(GameProgress::Get().lastMap, 1, StageConfig::kStageCount);   // 前回選んだマップの景色から
        m_RunTier = std::clamp(m_RunTier, 1, StageConfig::kMaxTier);
        m_RunPos = std::clamp(m_RunPos, 1, m_RunTier);
    }
    const StageDef& stageDef = StageConfig::Get(m_StageIndex);
    m_TerrainConfig.biome = stageDef.biome;
    TerrainGenerator::ApplyBiomeProfile(m_TerrainConfig);   // マップ毎の地形の規則（2026-10-26）
    m_Lighting.ApplyPreset(stageDef.light);
    std::cout << "[CollisionTestScene] stage " << m_StageIndex << " biome " << (int)stageDef.biome << std::endl;
    if (m_AutoTest)
    {
        char line[48];
        snprintf(line, sizeof(line), "stage %d biome %d", m_StageIndex, (int)stageDef.biome);
        AutoTestLog(line);   // TEMP-TEST: 面の引き継ぎの確認用
    }
}

// ============================================================
// 地形と、その上に乗る物（ミニマップの絵・置物・草・草の火・落ち葉・風に流される物）
// ============================================================
void CollisionTestScene::InitTerrainWorld(const StageDef& stageDef, ID3D11Device* device, ID3D11DeviceContext* context)
{
    // ---------- 地形（グリッド整列。台地と坂道の野原）----------
    // フィールド: 100 x 100 マス = 200m x 200m（山頂・平原・洞窟の三層。2026-10-02 に一度 300m にしたが、
    // 10-03 ユーザー「大きすぎる」で 200m へ戻した。高低差はそのまま、水平だけ 2/3）。
    // 開始時ごとに seed を変える（同じ seed なら同じ地形。VFXL_TERRAIN_SEED で固定できる）
    m_Grid.Init(100, 100);
    {
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_TERRAIN_SEED", env, sizeof(env)) > 0)
            m_TerrainConfig.seed = (uint32_t)strtoul(env, nullptr, 10);
        else
            m_TerrainConfig.seed = std::random_device{}() % 100000u;
        // TEMP-TEST: VFXL_NO_RELIEF=1 で起伏を切る（2026-10-04 以前の平らな地面と見比べる用）
        if (GetEnvironmentVariableA("VFXL_NO_RELIEF", env, sizeof(env)) > 0)
            m_TerrainConfig.relief = false;
        // TEMP-TEST: VFXL_STEP_NOISE=n 段々の台地の輪郭のノイズの種類（見比べる用。2026-10-20）
        if (GetEnvironmentVariableA("VFXL_STEP_NOISE", env, sizeof(env)) > 0)
            m_TerrainConfig.stepNoise = std::clamp(atoi(env), 0, 2);
        // TEMP-TEST: 外周の岩を縁から内に入れない = 岩の衝突 0 個（衝突の有無で fps を比べる用）
        if (GetEnvironmentVariableA("VFXL_NO_EDGE_COLLIDERS", env, sizeof(env)) > 0)
        {
            m_TerrainConfig.edgeRockIntrudeMin = -1.0f;
            m_TerrainConfig.edgeRockIntrudeMax = 0.0f;
        }
    }
    // 保存した地図を使う面（StageDef::mapFile）はそれを読む。VFXL_MAP=<名前> で上書き（デバッグ）。無ければ seed から生成
    m_MapFile = stageDef.mapFile ? stageDef.mapFile : "";
    // 地図エディタ（F6）の「Save and play」で指定された地図（戦闘の Terrain パネルの Regenerate で外れる）
    if (!MapData::PlayOverride().empty()) m_MapFile = MapData::PlayOverride();
    if (m_LabScene)
    {
        // 実験場：縁の碗だけの平らな野原。地図は読まない
        m_MapFile.clear();
        m_TerrainConfig.layers = false;
        m_TerrainConfig.relief = false;
        m_TerrainConfig.steps = false;   // 段々の台地も無し（平らな実験場）
        m_TerrainConfig.plateauCount = 0;
        m_TerrainConfig.terraceCount = 0;
        m_TerrainConfig.treeCount = 0;
        m_TerrainConfig.rockCount = 0;
        m_TerrainConfig.bushCount = 0;
    }
    {
        char env[64] = {};
        if (GetEnvironmentVariableA("VFXL_MAP", env, sizeof(env)) > 0) m_MapFile = env;
    }
    if (IsMenuScene())
    {
        // タイトル・ステージ選択：決まった seed（地図・環境変数は見ない）
        m_MapFile.clear();
        m_TerrainConfig.seed = kTitleSeed;
    }
    std::vector<uint8_t> grassMask;
    BuildTerrain(grassMask);
    BlockUnreachablePockets();
    m_GameUI.BakeMinimap(m_Grid, m_TerrainMap, (int)m_TerrainConfig.biome, m_TerrainConfig.seed);   // 右上のミニマップの絵
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
    // 草の延焼（草の無い面では止めておく）
    if (!m_GrassFire.Initialize(device, context))
        std::cout << "[Error] GrassFireField init failed" << std::endl;
    m_GrassFire.SetActive(stageDef.grass && m_Grass.GetSettings().enabled);
    m_GrassFire.Build(m_Grid);
    // 地面の落ち葉（風が触れる物。草の高さテクスチャに乗る）
    if (!m_Debris.Initialize(device, context))
        std::cout << "[Error] GroundDebris init failed" << std::endl;
    m_Debris.Build(m_Grid, m_TerrainConfig.seed, m_TerrainConfig.biome, m_Grass.GetHeightSRV());
    // 風に流される物（2026-10-22）：砂漠・遺跡の転がり草、草原のタンポポ
    m_Tumbleweeds.Init(device);
    if (!m_Dandelions.Initialize(device, context))
        std::cout << "[Error] DandelionField init failed" << std::endl;
    BuildWindToys();
}

// ============================================================
// GPU 側の gameplay（雑魚・弾・範囲の表）、難度、敵の混ざり方。地形の後（格子表をそのまま上げる）
// ============================================================
void CollisionTestScene::InitSwarm(const StageDef& stageDef, ID3D11Device* device, ID3D11DeviceContext* context)
{
    // ---------- GPU 側 gameplay（雑魚・投射物・オーブ）----------
    // ※地形の生成後に呼ぶ。格子表をそのまま上げるため
    m_Swarm.SetParticleSystem(&m_ParticleSystem);
    if (!m_Swarm.Initialize(device, context))
        std::cout << "[Error] SwarmSystem init failed" << std::endl;

    m_Swarm.UploadTerrain(m_Grid);
    m_Swarm.BuildVFXTable();
    // 範囲攻撃と投射物の飛び方（エディタで作った json）を GPU の表へ。
    // 先に範囲：投射物の「命中で出す範囲」が範囲の番号を引くため
    AreaProfileDB::LoadAll();
    m_Swarm.SetAreaDefs(AreaProfileDB::BuildDefs(m_Swarm.GetVFXTable()));
    m_GrassFire.BuildHeatTable(m_Swarm.GetVFXTable());   // 範囲の heat → レシピ毎の表（範囲の profile と VFX 表の後）
    ProjectileProfileDB::LoadAll();
    m_Swarm.SetMotions(ProjectileProfileDB::BuildMotions());
    m_WeaponSystem.SetAreaVFX(&m_AreaVFX, &m_VFXContext);
    // CPU で再生するエフェクトの雛形（と Mesh entry の FBX）を先に読む。初回の再生で読むと 1 フレーム飛ぶ
    for (int i = 0; i < VFXDatabase::Count(); ++i)
    {
        const VFXId id = VFXDatabase::At(i);
        if (!VFXDatabase::IsGpu(id)) m_AreaVFX.Preload(VFXDatabase::GetFile(id));
    }
    m_Mobs.Init(m_Swarm);
    m_Audio.Init(m_Swarm, m_StageIndex);   // 範囲の音の表（範囲の profile と VFX 表の後）
    {
        // 難度 = 表 × 「何面目か」（PosDifficulty。2026-10-17 の面毎の値）× マップ自体（StageDef）× Tier（2026-10-21、ステージ選択）
        const auto& pos = StageConfig::PosDifficulty(m_RunPos - 1);
        const auto& tier = StageConfig::TierMul(m_RunTier);
        m_Mobs.stageTimeOffsetMin = pos.timeOffsetMin + stageDef.difficultyTimeOffsetMin;
        m_Mobs.stageHpMul = pos.hpMul * stageDef.difficultyHpMul * tier.hpMul;
        m_Mobs.stageDamageMul = pos.damageMul * stageDef.difficultyDamageMul * tier.damageMul;
        m_Mobs.stageSpawnMul = pos.spawnMul * stageDef.difficultySpawnMul;
        m_ExpMul = tier.expMul;
        std::cout << "[CollisionTestScene] run map " << m_StageIndex << " tier " << m_RunTier << " pos " << m_RunPos
            << " -> table +" << m_Mobs.stageTimeOffsetMin << " min HP x" << m_Mobs.stageHpMul << " dmg x" << m_Mobs.stageDamageMul
            << " spawn x" << m_Mobs.stageSpawnMul << " exp x" << m_ExpMul << std::endl;
    }
    m_Mobs.splitterStart = stageDef.splitter.start;   // スプリッターの混ざり方（面毎）
    m_Mobs.splitterRatioStart = stageDef.splitter.ratioStart;
    m_Mobs.splitterRatioEnd = stageDef.splitter.ratioEnd;
    m_Mobs.splitterRampEnd = stageDef.splitter.rampEnd;
    // 突撃兵・盾兵の混ざり方（面毎。2026-10-08）
    m_Mobs.chargerMix = { stageDef.charger.start, stageDef.charger.ratioStart, stageDef.charger.ratioEnd, stageDef.charger.rampEnd };
    m_Mobs.shieldMix = { stageDef.shield.start, stageDef.shield.ratioStart, stageDef.shield.ratioEnd, stageDef.shield.rampEnd };
    m_Mobs.spikeBallMix = { stageDef.spikeBall.start, stageDef.spikeBall.ratioStart, stageDef.spikeBall.ratioEnd, stageDef.spikeBall.rampEnd };   // トゲ玉（砂漠。2026-10-17）
    m_Mobs.burrowerMix = { stageDef.burrower.start, stageDef.burrower.ratioStart, stageDef.burrower.ratioEnd, stageDef.burrower.rampEnd };   // 砂潜り（砂漠。2026-10-17）
    m_Mobs.sandMageMix = { stageDef.sandMage.start, stageDef.sandMage.ratioStart, stageDef.sandMage.ratioEnd, stageDef.sandMage.rampEnd };   // 砂嵐の術師（砂漠。2026-10-17）
    m_Mobs.dashBomberMix = { stageDef.dashBomber.start, stageDef.dashBomber.ratioStart, stageDef.dashBomber.ratioEnd, stageDef.dashBomber.rampEnd };   // 突撃爆弾兵（全ステージ。2026-10-18）
    m_Mobs.armorMix = { stageDef.armor.start, stageDef.armor.ratioStart, stageDef.armor.ratioEnd, stageDef.armor.rampEnd };   // 遺跡の敵（2026-10-25）
    m_Mobs.gargoyleMix = { stageDef.gargoyle.start, stageDef.gargoyle.ratioStart, stageDef.gargoyle.ratioEnd, stageDef.gargoyle.rampEnd };
    m_Mobs.necroMix = { stageDef.necro.start, stageDef.necro.ratioStart, stageDef.necro.ratioEnd, stageDef.necro.rampEnd };
    m_Mobs.wardenMix = { stageDef.warden.start, stageDef.warden.ratioStart, stageDef.warden.ratioEnd, stageDef.warden.rampEnd };
    m_WeaponSystem.SetSwarm(&m_Swarm);
    m_WeaponSystem.groundHeight = [this](float x, float z) { return m_Grid.SampleHeight(x, z); };   // アイススパイクの線を地面に合わせる
}

// ============================================================
// プレイヤー（前の面の引き継ぎも）と反応のエフェクト
// ============================================================
void CollisionTestScene::InitPlayer(const StageDef& stageDef, ID3D11Device* device)
{
    // ============================================================
    // プレイヤー
    // 組み立ては PlayerFactory に任せる。
    // シーンはどの Component が付いているかを知らなくてよい。
    // ============================================================
    PlayerFactory::Config pcfg;
    pcfg.color = { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f };
    {
        // TEMP-TEST: VFXL_PLAYER=ranger で前のレンジャーに戻す（2026-10-04 から既定は Shadowkin、PlayerFactory.h）
        char env[32] = {};
        if (GetEnvironmentVariableA("VFXL_PLAYER", env, sizeof(env)) > 0 && _stricmp(env, "ranger") == 0)
        {
            pcfg.skinnedModel = Res::Mdl::Quaternius_Ranger;
            pcfg.modelScale = 1.0f;
            pcfg.modelYawOffsetDeg = 0.0f;
        }
    }

    // TEMP-TEST: VFXL_DUMP_CELL=gx,gz で周り 9x9 マスの高さ（0.5m の高さマスの最小 / 最大）と歩けるかを autotest.log へ
    //（起伏で雑魚が詰まる所の地形を見る用、2026-10-05）
    {
        char env[32] = {};
        if (GetEnvironmentVariableA("VFXL_DUMP_CELL", env, sizeof(env)) > 0)
        {
            int cx = 0, cz = 0;
            if (sscanf_s(env, "%d,%d", &cx, &cz) == 2)
            {
                const int sub = GridWorld::kHeightSub;
                for (int z = cz + 4; z >= cz - 4; --z)
                {
                    std::string row = "cell row z " + std::to_string(z) + ":";
                    for (int x = cx - 4; x <= cx + 4; ++x)
                    {
                        float lo = 1e9f, hi = -1e9f;
                        for (int sz = 0; sz < sub; ++sz)
                            for (int sx = 0; sx < sub; ++sx)
                            {
                                const float h = m_Grid.HeightAt(x * sub + sx, z * sub + sz);
                                lo = (std::min)(lo, h);
                                hi = (std::max)(hi, h);
                            }
                        char b[48];
                        snprintf(b, sizeof(b), " %c%5.1f/%5.1f", m_Grid.IsWalkable(x, z) ? ' ' : '#', lo, hi);
                        row += b;
                    }
                    AutoTestLog(row.c_str());
                }
            }
        }
    }
    m_Player = PlayerFactory::Create(m_Registry, device, pcfg);
    m_ClothChainSystem.groundHeight = [this](float x, float z) { return m_Grid.SampleHeight(x, z); };
    ApplyRunCarry();   // 前の面のバックパック・魔法書・等級・能力値（あれば）
    if (m_Registry.Has<WalletComponent>(m_Player))
        m_GoldAtStart = m_Registry.Get<WalletComponent>(m_Player).earned;   // リザルトの「この面で拾った金貨」用

    // ---------- 反応のエフェクト（レベルアップ・開箱・被弾）----------
    m_Feedback.Init(&m_AreaVFX, &m_VFXContext);
    m_PlayerVFX.Init(&m_AreaVFX, &m_VFXContext);
    m_SpellFx.Init(&m_AreaVFX, &m_VFXContext);
    // 土煙の色を面に合わせる（草原は土色のまま、砂漠は砂、遺跡は灰）
    if (stageDef.biome == TerrainGenerator::Biome::Desert)       m_PlayerVFX.dustTint = { 1.18f, 1.08f, 0.86f };
    else if (stageDef.biome == TerrainGenerator::Biome::Dungeon) m_PlayerVFX.dustTint = { 0.88f, 0.88f, 0.94f };
    if (m_AutoTest)
        m_Feedback.onPlayed = [this](const char* file) { AutoTestLog(file); };   // TEMP-TEST
}

// ============================================================
// 面の流れ：報酬の箱・門・Boss・磁石・天候、実験場 / タイトル / ステージ選択、初回のチュートリアル。
// 箱はプレイヤーの位置が要るので最後
// ============================================================
void CollisionTestScene::InitStageFlow(const StageDef& stageDef, ID3D11Device* device)
{
    // ---------- 報酬の箱（プレイヤーの周りに固定数。プレイヤーの位置が要るので最後）----------
    m_Crates.Init();
    m_Stage.Init();
    m_BossAttacks.Reset();
    InitBossFx(stageDef.BossAt(m_RunPos));   // 面の Boss（技の組み合わせ・召喚・体の色。2026-10-24。何面目かで替わる 10-26）
    m_BossSlamHits = 0;
    m_Pickups.Init(device);
    if (m_LabScene || IsMenuScene())
    {
        // 実験場・タイトル・ステージ選択：制限時間・エリート・箱・門・磁石なし。箱と門は置かない（RespawnCrates を呼ばない）
        m_Stage.stageTime = 0.0f;
        m_Stage.eliteTimes.clear();
        m_Stage.waves.enabled = false;   // 湧きの波も無し（湧き自体が止まっているが、案内が出ないように）
        m_Pickups.initialCount = 0;
        m_Pickups.maxOnMap = 0;
    }
    else
        RespawnCrates();

    // ---------- 天候（昼 → 夕 → 夜、面毎の出来事。照明を毎フレーム上書きする）----------
    m_Weather.Init(stageDef, m_Stage.stageTime, m_Lighting, m_AreaVFX, &m_VFXContext, &m_Swarm);
    if (m_LabScene)
    {
        // 実験場：昼のまま、天候の出来事なし。湧き停止・無敵・MP 無限・経験値 0 と 9x9 の枠は SpellLab が受け持つ
        m_Weather.driveLighting = false;
        m_Weather.eventsEnabled = false;
        // 2026-10-07 から的・群れ・Boss・木箱はゲームの UI（トレーニングのメニュー、T / パッド RB）。
        // それまでは ImGui の窓を触るのでカーソルを出していた（SetCursorFree(true)）。今は普段どおり視点を回す
        m_GameUI.SetTrainingEnabled(true);
        m_SpellLab.EnterLab(m_Registry, m_Player, m_Swarm, m_Mobs);
    }
    if (m_TitleScene) InitTitle();     // 丘の上のプレイヤー・夕方の光・回るカメラ・メニュー（CollisionTestSceneTitle.cpp）
    if (m_SelectScene) InitSelect();   // 同じ景色 + ステージ選択の画面（CollisionTestSceneSelect.cpp）

    // ---- 初回のチュートリアル（2026-10-13）：タイトルから第 1 ステージに入った最初の 1 回だけ。
    // 終えた / 飛ばしたら GameSettings::tutorialDone。VFXL_TUTORIAL=1 / 0 で強制（TEMP-TEST）。自動テスト中は出さない
    if (!m_LabScene && !IsMenuScene())
    {
        char env[8] = {};
        const bool hasEnv = GetEnvironmentVariableA("VFXL_TUTORIAL", env, sizeof(env)) > 0;
        const bool start = hasEnv ? (env[0] == '1')
            : (!GameSettings::Get().tutorialDone && m_StageIndex == 1 && !m_AutoTest);
        if (start) m_Tutorial.Start();
    }
}

// ============================================================
// 前の面から引き継いだプレイヤー（バックパック・魔法書・等級・能力値）。HP / MP は上限から始める
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
        lv.ClearChoices();
    }
    if (m_Registry.Has<WalletComponent>(m_Player))
        m_Registry.Get<WalletComponent>(m_Player) = g_RunCarry.wallet;   // 金貨は次の面へ持ち越す
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
    if (m_Registry.Has<ShieldComponent>(m_Player))
    {
        auto& sh = m_Registry.Get<ShieldComponent>(m_Player);
        sh.max = g_RunCarry.shield.max;
        sh.rechargeDelay = g_RunCarry.shield.rechargeDelay;
        sh.refillTime = g_RunCarry.shield.refillTime;
        sh.current = sh.max;
        sh.sinceHit = sh.rechargeDelay;
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
