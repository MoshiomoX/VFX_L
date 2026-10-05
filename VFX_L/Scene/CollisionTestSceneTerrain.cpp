// ============================================================
// CollisionTestSceneTerrain.cpp
// CollisionTestScene: 地形を建てる / 建て直す（seed から生成、または保存した地図 MapData から）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Core/Application.h"
#include "Component/InteractableComponent.h"
#include "Component/TransformComponent.h"
#include "ECS/View.h"
#include <iostream>

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
            return;
        }
        std::cout << "[CollisionTestScene] map '" << m_MapFile << "' could not be used; generating from seed" << std::endl;
    }

    TerrainGenerator::Generate(m_Registry, device, m_Grid, m_TerrainConfig, m_Terrain, &grassMask, &m_Torches,
        &m_TerrainLayout, &m_TerrainMap);
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

    std::vector<uint8_t> grassMask;
    BuildTerrain(grassMask);
    BlockUnreachablePockets();
    m_StaticProps.Build(m_Registry);   // 置物の instanced 表も作り直す
    m_Grass.Build(m_Grid, grassMask, m_TerrainConfig.seed, m_TerrainConfig.biome);   // 草の高さ・色・生やす所も

    // GPU 側の格子表も差し替える（古い表のままだと弾が壁を抜ける）
    m_Swarm.KillAll();
    m_Swarm.UploadTerrain(m_Grid);
    m_Swarm.BuildVFXTable();
    RespawnElites();
    RespawnCrates();   // 古い位置は新しい壁の中かもしれない
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
