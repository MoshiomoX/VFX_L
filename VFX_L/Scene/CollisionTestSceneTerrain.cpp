// ============================================================
// CollisionTestSceneTerrain.cpp
// CollisionTestScene: 地形を建てる / 建て直す（seed から生成、または保存した地図 MapData から）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Core/Application.h"
#include "Component/InteractableComponent.h"
#include "Component/TransformComponent.h"
#include "Component/RigidbodyComponent.h"
#include "ECS/View.h"

// ============================================================
// 地形を建てる
// m_MapFile があれば Assets/Data/MapData/<名前>.vmap を読んで建てる（無い・壊れている時は生成へ落ちる）。
// どちらの場合も建てた物の記録を m_TerrainMap に持つ（Terrain パネルの Save Map が書き出す）
// ============================================================
void CollisionTestScene::BuildTerrain(std::vector<uint8_t>& grassMask)
{
    auto* device = Application::Get().GetGraphics().GetDevice();
    m_Torches.clear();
    m_MapLoaded = false;

    if (!m_MapFile.empty())
    {
        MapData::Map file;
        if (MapData::Load(m_MapFile, file)
            && TerrainGenerator::BuildFromMap(m_Registry, device, m_Grid, file, m_Terrain, &grassMask, &m_Torches,
                &m_TerrainLayout, &m_TerrainMap))
        {
            // 箱・Boss の門・磁石の配置と草の色は seed と面の見た目から決まる：地図の物に合わせる
            m_TerrainConfig.seed = file.seed;
            m_TerrainConfig.biome = (TerrainGenerator::Biome)file.biome;
            m_MapLoaded = true;
            m_GrassMask = grassMask;
            return;
        }
        std::cout << "[CollisionTestScene] map '" << m_MapFile << "' could not be used; generating from seed" << std::endl;
    }

    TerrainGenerator::Generate(m_Registry, device, m_Grid, m_TerrainConfig, m_Terrain, &grassMask, &m_Torches,
        &m_TerrainLayout, &m_TerrainMap);
    m_GrassMask = grassMask;
}

// ============================================================
// 地形の建て直し
// 古い地形を全部消して作り直す。
// ※GPU 側は KillAll で全消し（雑魚・弾・オーブ）。counter は残るので撃破数などの累計は続く
// ============================================================
void CollisionTestScene::RebuildTerrain()
{
    for (Entity e : m_Terrain)
        if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
    m_Terrain.clear();
    m_Grid.ClearAll();
    m_PropBlocks.clear();   // 新しい格子に古い置物のマスを戻さない

    // TEMP-TEST：各段の ms（ステージ選択の切替が重い、2026-10-21）
    using Clock = std::chrono::steady_clock;
    auto t = Clock::now();
    char prof[256] = {};
    auto lap = [&](const char* name)
        {
            const auto now = Clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - t).count();
            t = now;
            char buf[48];
            snprintf(buf, sizeof(buf), "%s %.0f ", name, ms);
            strncat_s(prof, buf, _TRUNCATE);
        };

    std::vector<uint8_t> grassMask;
    BuildTerrain(grassMask);
    lap("gen");
    BlockUnreachablePockets();
    lap("pockets");
    if (!IsMenuScene())   // タイトル・ステージ選択は HUD が無い（Debug で 190 ms。2026-10-21）
        m_GameUI.BakeMinimap(m_Grid, m_TerrainMap, (int)m_TerrainConfig.biome, m_TerrainConfig.seed);   // ミニマップも新しい地形に
    lap("minimap");
    m_StaticProps.Build(m_Registry);   // 置物の instanced 表も作り直す
    lap("props");
    m_Grass.Build(m_Grid, grassMask, m_TerrainConfig.seed, m_TerrainConfig.biome);   // 草の高さ・色・生やす所も
    m_GrassFire.Build(m_Grid);   // 火も消す
    m_Debris.Build(m_Grid, m_TerrainConfig.seed, m_TerrainConfig.biome, m_Grass.GetHeightSRV());   // 落ち葉も置き直す
    lap("grass");

    // GPU 側の格子表も差し替える（古い表のままだと弾が壁を抜ける）
    m_Swarm.KillAll();
    m_Swarm.UploadTerrain(m_Grid);
    m_Swarm.BuildVFXTable();
    m_GrassFire.BuildHeatTable(m_Swarm.GetVFXTable());
    lap("swarm");
    m_RebuildProfile = prof;
    if (!IsMenuScene()) RespawnElites();   // デバッグの的（無敵のエリート）を置き直す。ステージ選択の景色には出さない（2026-10-21：切替後に浮いた骸骨が見えていた）
    if (!m_LabScene && !IsMenuScene()) RespawnCrates();   // 古い位置は新しい壁の中かもしれない。実験場・タイトル・ステージ選択は箱 / 門 / 磁石なし
}

// ============================================================
// トレーニングのフィールド切替（2026-10-20）
// 面の見た目（Biome・照明・草）を入れ替えて地形を建て直す。平地 = 実験場の既定（Init と同じ切り方）
// ============================================================
std::wstring CollisionTestScene::EnterTrainingField(int field, uint32_t seed)
{
    const bool flat = field <= 0;
    const int stage = flat ? 1 : std::clamp(field, 1, StageConfig::kStageCount);
    const StageDef& def = StageConfig::Get(stage);
    m_StageIndex = stage;

    TerrainGenerator::Config cfg;   // 既定 = 段々の台地の野原
    cfg.seed = (seed != 0) ? seed : std::random_device{}() % 100000u;   // ステージ選択は決まった seed（2026-10-21）
    cfg.biome = def.biome;
    if (flat)
    {
        cfg.layers = false;
        cfg.relief = false;
        cfg.steps = false;
        cfg.plateauCount = 0;
        cfg.terraceCount = 0;
        cfg.treeCount = 0;
        cfg.rockCount = 0;
        cfg.bushCount = 0;
    }
    m_TerrainConfig = cfg;
    m_MapFile.clear();
    MapData::PlayOverride().clear();

    // 面の照明と草（Init と同じ所から取る）。天候は実験場では照明を上書きしない
    m_Lighting.ApplyPreset(def.light);
    auto& gs = m_Grass.GetSettings();
    gs.enabled = def.grass;
    gs.spacing = def.grassSpacing;
    gs.rootColor = def.grassRoot;
    gs.tipColor = def.grassTip;
    gs.heightMin = def.grassHeightMin;
    gs.heightMax = def.grassHeightMax;
    m_GrassFire.SetActive(def.grass && gs.enabled);
    m_Audio.Init(m_Swarm, m_StageIndex);   // 面の曲（範囲の音の表も作り直す。中身は同じ）

    RebuildTerrain();   // 実験場なので箱 / 門 / 磁石は置かない（RebuildTerrain が m_LabScene を見る）

    // プレイヤーは中央の地面へ（前の地形の高さのままだと埋まる / 浮く）
    if (m_Registry.IsValid(m_Player) && m_Registry.Has<TransformComponent>(m_Player))
    {
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        tf.position = Vector3(0.0f, m_Grid.SampleHeight(0.0f, 0.0f) + 1.0f, 0.0f);
        if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        m_Camera.Camera().SnapToTarget();
    }

    wchar_t buf[96];
    swprintf_s(buf, L"フィールド：%ls（seed %u）", flat ? L"平地" : def.name, cfg.seed);
    return buf;
}

// ============================================================
// 今ある報酬の箱と Boss の門の位置を地図の記録へ（Terrain パネルの Save Map）
// seed から並べた物を地図に固めておくと、F6 の地図エディタで動かせる。
// その種類が地図に 1 つでもあれば（エディタで置いた物）触らない
// ============================================================
void CollisionTestScene::BakePlacements()
{
    bool hasCrate = false, hasGate = false;
    for (const auto& p : m_TerrainMap.placements)
        (p.type == MapData::kPlaceBossGate ? hasGate : hasCrate) = true;

    m_Registry.CreateView<InteractableComponent>()
        .Each([&](Entity e, InteractableComponent& it)
        {
            MapData::Placement p;
            p.pos = it.basePos;
            if (it.kind == InteractKind::RewardChoice && !hasCrate)
            {
                p.type = MapData::kPlaceCrate;
                if (m_Registry.Has<TransformComponent>(e)) p.yawDeg = m_Registry.Get<TransformComponent>(e).rotation.y;
                m_TerrainMap.placements.push_back(p);
            }
            else if (it.kind == InteractKind::BossPortal && !hasGate)
            {
                p.type = MapData::kPlaceBossGate;
                p.yawDeg = m_Stage.GetPortalYaw();
                m_TerrainMap.placements.push_back(p);
            }
        });
}
