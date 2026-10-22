// ============================================================
// CollisionTestSceneSelect.cpp
// ステージ選択（StageSelectScene = m_SelectScene。2026-10-21 ユーザー：「ステージ選択を Megabonk に完全に倣う」→ 独立したシーン）。
//   景色はタイトルと同じ作り（PlaceTitleStand / UpdateTitleWorld / UpdateTitleCamera）：選んだマップの地形（決まった seed）を
//   プレイヤーの周りでカメラが回る。戦いの見本は無し。手前に StageSelectUI（マップ × Tier）。
//   マップのカーソルが動いたら EnterTrainingField で地形・光・草を作り直す（Debug で 1〜2 秒止まる）。
//   出発：g_RunCarry に計画（開始マップ・Tier）を積んで戦闘へ。戻る：タイトルへ。
//   Debug（F10 のパネル表示中）：「Unlock all」「Reset progress」の小さな ImGui 窓
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Scene/RunResult.h"
#include "Component/ModelComponent.h"
#include "Component/ColliderComponent.h"
#include "Core/GameProgress.h"
#include "Core/DevUI.h"
#include "Core/Application.h"
#include "Debug/DebugManager.h"
#include "Audio/AudioSystem.h"

#include "imgui.h"
#include <Windows.h>
#include <chrono>

namespace
{
    // TEMP-TEST: VFXL_SELECT_AUTOTEST=1 で画面を順に見せて出発する（スクリーンショット用。キーを送らない）
    bool SelectAutoTest()
    {
        char env[8] = {};
        return GetEnvironmentVariableA("VFXL_SELECT_AUTOTEST", env, sizeof(env)) > 0 && env[0] == '1';
    }
}

// ============================================================
// Init の終わり（地形・プレイヤー・天候ができた後）
// ============================================================
// ============================================================
// 3 つのマップの地形を全部持っておく（切替を瞬時に）
// ============================================================
struct CollisionTestScene::SelectWorlds
{
    struct World
    {
        int stage = 0;
        TerrainGenerator::Config config;
        GridWorld grid;
        MapData::Map map;
        TerrainGenerator::Layout layout;
        std::vector<Vector3> torches;
        std::vector<uint8_t> grassMask;
        std::vector<Entity> entities;
        // しまっている間に預かる部品（戻す時にそのまま Add する）
        std::vector<std::pair<Entity, ModelComponent>> parkedModels;
        std::vector<std::pair<Entity, ColliderComponent>> parkedColliders;
        bool parked = false;
    };
    World worlds[StageConfig::kStageCount];
    int active = -1;   // worlds の添字。-1 = まだ

    static void Park(Registry& reg, World& w)
    {
        if (w.parked) return;
        for (Entity e : w.entities)
        {
            if (!reg.IsValid(e)) continue;
            if (reg.Has<ModelComponent>(e)) { w.parkedModels.emplace_back(e, reg.Get<ModelComponent>(e)); reg.Remove<ModelComponent>(e); }
            if (reg.Has<ColliderComponent>(e)) { w.parkedColliders.emplace_back(e, reg.Get<ColliderComponent>(e)); reg.Remove<ColliderComponent>(e); }
        }
        w.parked = true;
    }
    static void Unpark(Registry& reg, World& w)
    {
        if (!w.parked) return;
        for (auto& [e, m] : w.parkedModels)    if (reg.IsValid(e)) reg.Add<ModelComponent>(e, m);
        for (auto& [e, c] : w.parkedColliders) if (reg.IsValid(e)) reg.Add<ColliderComponent>(e, c);
        w.parkedModels.clear();
        w.parkedColliders.clear();
        w.parked = false;
    }
};

void CollisionTestScene::BuildSelectWorlds()
{
    m_SelectWorlds = std::make_shared<SelectWorlds>();
    auto& S = *m_SelectWorlds;
    auto capture = [&](SelectWorlds::World& w, int stage)
        {
            w.stage = stage;
            w.config = m_TerrainConfig;
            w.grid = m_Grid;
            w.map = m_TerrainMap;
            w.layout = m_TerrainLayout;
            w.torches = m_Torches;
            w.grassMask = m_GrassMask;
            w.entities = m_Terrain;
        };
    // Init が建てた物（前回選んだマップ）
    const int first = m_StageIndex;
    capture(S.worlds[first - 1], first);
    SelectWorlds::Park(m_Registry, S.worlds[first - 1]);
    // 残りの 2 つ：Init と同じ建て方（決まった seed、地図ファイルは見ない）
    for (int stage = 1; stage <= StageConfig::kStageCount; ++stage)
    {
        if (stage == first) continue;
        const auto t0 = std::chrono::steady_clock::now();
        m_Terrain.clear();   // 前の地形の実体は預かってある（消さない）
        m_Grid.ClearAll();
        m_PropBlocks.clear();
        TerrainGenerator::Config cfg;
        cfg.seed = kTitleSeed;
        cfg.biome = StageConfig::Get(stage).biome;
        m_TerrainConfig = cfg;
        m_MapFile.clear();
        m_StageIndex = stage;
        std::vector<uint8_t> mask;
        BuildTerrain(mask);
        BlockUnreachablePockets();
        capture(S.worlds[stage - 1], stage);
        SelectWorlds::Park(m_Registry, S.worlds[stage - 1]);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "[StageSelect] prebuilt map " << stage << " in " << ms << " ms" << std::endl;
    }
    m_StageIndex = first;
}

void CollisionTestScene::ActivateSelectWorld(int map)
{
    if (!m_SelectWorlds) return;
    auto& S = *m_SelectWorlds;
    map = std::clamp(map, 1, StageConfig::kStageCount);
    if (S.active == map - 1) return;
    if (S.active >= 0) SelectWorlds::Park(m_Registry, S.worlds[S.active]);
    SelectWorlds::World& w = S.worlds[map - 1];
    SelectWorlds::Unpark(m_Registry, w);
    S.active = map - 1;

    // ---- 場（格子・記録・松明）を差し替える ----
    m_StageIndex = map;
    m_TerrainConfig = w.config;
    m_Grid = w.grid;
    m_TerrainMap = w.map;
    m_TerrainLayout = w.layout;
    m_Torches = w.torches;
    m_GrassMask = w.grassMask;
    m_Terrain = w.entities;
    m_PropBlocks.clear();
    m_MapLoaded = false;
    m_MapFile.clear();
    MapData::PlayOverride().clear();

    // ---- 面の草の設定（EnterTrainingField と同じ）→ 置物・草・落ち葉の表、GPU の格子表 ----
    const StageDef& def = StageConfig::Get(map);
    auto& gs = m_Grass.GetSettings();
    gs.enabled = def.grass;
    gs.spacing = def.grassSpacing;
    gs.rootColor = def.grassRoot;
    gs.tipColor = def.grassTip;
    gs.heightMin = def.grassHeightMin;
    gs.heightMax = def.grassHeightMax;
    m_StaticProps.Build(m_Registry);
    m_Grass.Build(m_Grid, m_GrassMask, m_TerrainConfig.seed, m_TerrainConfig.biome);
    m_GrassFire.Build(m_Grid);
    m_GrassFire.SetActive(def.grass && gs.enabled);
    m_Debris.Build(m_Grid, m_TerrainConfig.seed, m_TerrainConfig.biome, m_Grass.GetHeightSRV());
    m_Swarm.KillAll();
    m_Swarm.UploadTerrain(m_Grid);

    // ---- 立つ所・カメラの中心・光 ----
    PlaceTitleStand();
    ApplySelectLight();
    m_TitleTime = 0.0f;   // カメラは最初の向きから
}

void CollisionTestScene::InitSelect()
{
    auto& gfx = Application::Get().GetGraphics();
    m_Mobs.Director().enabled = false;   // 湧かない
    m_Audio.musicAuto = false;           // 曲はタイトルのまま
    BuildSelectWorlds();                 // 3 つとも建てて預かる（Debug で +2.5 秒ほど）
    ActivateSelectWorld(m_StageIndex);   // 前回選んだマップを出す（立つ所・光もここで）

    m_TitleTime = 0.0f;
    SetCamera(&m_TitleCam);
    UpdateTitleCamera(0.0f);
    m_SelectUI.Initialize(gfx.GetDevice(), gfx.GetContext(), m_ScreenW, m_ScreenH);
    const auto& p = GameProgress::Get();
    m_SelectUI.SetSelection(m_StageIndex, p.lastTier);
    AudioSystem::Get().PlayMusic("title");
}

// 草原はタイトルと同じ午後の光、他はそのマップの照明（天候は上書きしない）
void CollisionTestScene::ApplySelectLight()
{
    if (m_StageIndex == 1)
        ApplyTitleLight(StageConfig::Get(1).light);
    else
    {
        m_Weather.driveLighting = false;
        m_Weather.eventsEnabled = false;
        m_Lighting.ApplyPreset(StageConfig::Get(m_StageIndex).light);
    }
}

// ============================================================
// Update の代わり：景色 + 選択の画面
// ============================================================
void CollisionTestScene::UpdateSelect(float dt)
{
    UpdateTitleWorld(dt, false);

    // ---- マップが変わった（カーソルが止まって少し）：預かってある地形を出す（建て直さない）----
    if (m_SelectUI.ConsumeMapChanged() && m_SelectUI.Map() != m_StageIndex)
    {
        const int map = m_SelectUI.Map();
        const auto t0 = std::chrono::steady_clock::now();
        ActivateSelectWorld(map);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "[StageSelect] switch map " << map << " took " << ms << " ms" << std::endl;
        if (SelectAutoTest())
        {
            char line[128];
            snprintf(line, sizeof(line), "select switch map %d ms %.0f", map, ms);
            AutoTestLog(line);
        }
    }

    // ---- Debug：進み具合の操作（F10 でパネル表示中だけ）----
    if constexpr (DevUI::kEnabled)
    {
        if (DebugManager::Get().GetShowPanels())
        {
            auto& p = GameProgress::Get();
            ImGui::Begin("Stage Select (debug)");
            ImGui::Text("map %d tier %d", m_SelectUI.Map(), m_SelectUI.Tier());
            for (int m = 1; m <= GameProgress::kMaps; ++m)
                ImGui::Text("map %d: %s  tiers cleared %d%d%d", m, p.IsMapUnlocked(m) ? "open" : "locked",
                    p.IsTierCleared(m, 1), p.IsTierCleared(m, 2), p.IsTierCleared(m, 3));
            if (ImGui::Button("Unlock all")) { p.UnlockAll(); p.Save(); }
            ImGui::SameLine();
            if (ImGui::Button("Reset progress")) { p.ResetAll(); p.Save(); m_SelectUI.SetSelection(1, 1); }
            ImGui::End();
        }
    }

    // ---- 自動テスト（TEMP-TEST）：草原 → 砂漠 → 遺跡 と見せ、全部解放して草原 Tier 3 で出発 ----
    int picked = m_SelectUI.Update(dt);
    if (SelectAutoTest())
    {
        static int s_Step = 0;
        const float t = m_TitleTime;
        auto step = [&](int n, float at) { if (s_Step == n && t >= at) { ++s_Step; return true; } return false; };
        auto countAlive = [&]()
            {
                // 景色に敵が居てはいけない（切替後に見えた物の正体を調べる。2026-10-21）
                std::vector<Swarm::Enemy> enemies;
                std::vector<uint32_t> states;
                std::vector<Swarm::EnemyExtra> extras;
                int alive = 0;
                std::string kinds;
                if (m_Swarm.DebugReadEnemies(enemies, states, &extras))
                    for (size_t i = 0; i < states.size(); ++i)
                        if (states[i] != Swarm::kStateDead)
                        {
                            ++alive;
                            char k[48];
                            snprintf(k, sizeof(k), " [%u kind %u at %.1f,%.1f,%.1f]", (unsigned)i, extras.size() > i ? extras[i].kind : 999u,
                                enemies[i].position.x, enemies[i].position.y, enemies[i].position.z);
                            if (kinds.size() < 400) kinds += k;
                        }
                char line[512];
                snprintf(line, sizeof(line), "select enemies alive %d%s", alive, kinds.c_str());
                AutoTestLog(line);
            };
        if (step(0, 2.5f)) { AutoTestLog("select look map1"); countAlive(); }
        if (step(1, 3.0f)) m_SelectUI.TestSelect(2, 1);
        if (step(2, 6.5f)) { AutoTestLog("select look map2"); countAlive(); }
        if (step(3, 7.0f)) m_SelectUI.TestSelect(3, 1);
        if (step(4, 10.5f)) { AutoTestLog("select look map3"); countAlive(); }
        if (step(5, 11.0f)) { GameProgress::Get().UnlockAll(); m_SelectUI.TestSelect(1, 3); }   // 保存はしない
        if (step(6, 14.5f)) AutoTestLog("select look unlocked");
        if (step(7, 15.0f)) m_SelectUI.TestSelect(2, 1);   // 2 回目の砂漠（モデル・テクスチャが読めている時の純粋な生成時間）
        if (step(8, 18.5f)) m_SelectUI.TestSelect(1, 3);
        if (step(9, 22.0f))
        {
            char line[64];
            snprintf(line, sizeof(line), "select start map %d tier %d", m_SelectUI.Map(), m_SelectUI.Tier());
            AutoTestLog(line);
            AutoTestLog("select done");
            picked = StageSelectUI::kStart;
            m_SelectUI.TestSelect(1, 3);
        }
    }

    auto& sm = Application::Get().GetGame().GetSceneManager();
    switch (picked)
    {
    case StageSelectUI::kStart:
    {
        auto& p = GameProgress::Get();
        p.lastMap = m_SelectUI.Map();
        p.lastTier = m_SelectUI.Tier();
        if (!SelectAutoTest()) p.Save();
        g_RunCarry.Reset();
        g_RunCarry.startStage = m_SelectUI.Map();
        g_RunCarry.tier = m_SelectUI.Tier();
        g_RunCarry.stage = m_SelectUI.Map();
        std::cout << "[StageSelect] start map " << g_RunCarry.startStage << " tier " << g_RunCarry.tier << std::endl;
        sm.RequestChangeScene(SceneType::COLLISION_TEST);
        break;
    }
    case StageSelectUI::kBack:
        sm.RequestChangeScene(SceneType::TITLE);
        break;
    default:
        break;
    }
}
