// ============================================================
// BattleAutoTest.cpp
// TEMP-TEST: 戦闘シーンの自動テストの土台（シーンの部品への参照・名前の登録）
// ============================================================
#include "Debug/AutoTest/BattleAutoTest.h"
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    // 登録は各ファイルの静的初期化から来るので、表は関数の中に置く（初期化順に依らない）
    std::vector<std::pair<const char*, BattleAutoTest::Factory>>& Table()
    {
        static std::vector<std::pair<const char*, BattleAutoTest::Factory>> table;
        return table;
    }
}

BattleAutoTest::BattleAutoTest(CollisionTestScene& scene)
    : m_AutoStep(scene.m_AutoStep)
    , m_AutoTime(scene.m_AutoTime)
    , m_AutoInteract(scene.m_AutoInteract)
    , m_Registry(scene.m_Registry)
    , m_Player(scene.m_Player)
    , m_Camera(scene.m_Camera)
    , m_Grid(scene.m_Grid)
    , m_Swarm(scene.m_Swarm)
    , m_Mobs(scene.m_Mobs)
    , m_Stage(scene.m_Stage)
    , m_BossAttacks(scene.m_BossAttacks)
    , m_BossSlamHits(scene.m_BossSlamHits)
    , m_CollisionSystem(scene.m_CollisionSystem)
    , m_PlayerControlSystem(scene.m_PlayerControlSystem)
    , m_PlayerAnimSystem(scene.m_PlayerAnimSystem)
    , m_WeaponSystem(scene.m_WeaponSystem)
    , m_LevelUpSystem(scene.m_LevelUpSystem)
    , m_Crates(scene.m_Crates)
    , m_Pickups(scene.m_Pickups)
    , m_Audio(scene.m_Audio)
    , m_Outline(scene.m_Outline)
    , m_Stress(scene.m_Stress)
    , m_StaticProps(scene.m_StaticProps)
    , m_Grass(scene.m_Grass)
    , m_GameUI(scene.m_GameUI)
    , m_TerrainConfig(scene.m_TerrainConfig)
    , m_TerrainLayout(scene.m_TerrainLayout)
    , m_TerrainMap(scene.m_TerrainMap)
    , m_MapFile(scene.m_MapFile)
    , m_Terrain(scene.m_Terrain)
    , m_StageIndex(scene.m_StageIndex)
    , m_PortalVfx(scene.m_PortalVfx)
    , m_RunTime(scene.m_RunTime)
    , m_ExpGained(scene.m_ExpGained)
    , m_ScreenW(scene.m_ScreenW)
    , m_ScreenH(scene.m_ScreenH)
    , m_ShowWireframe(scene.m_ShowWireframe)
    , m_ShowWandDebug(scene.m_ShowWandDebug)
    , m_ShowGridDebug(scene.m_ShowGridDebug)
    , m_Scene(scene)
{
}

void BattleAutoTest::AutoTestLog(const char* what)
{
    m_Scene.AutoTestLog(what);
}

void BattleAutoTest::SetDecorPropsVisible(bool visible, int* outCount)
{
    // 野原の置物（木・石・低木・草）は StaticPropRenderer がまとめて描いている
    m_StaticProps.GetSettings().enabled = visible;
    if (outCount) *outCount = m_StaticProps.GetStats().registered;
}

bool BattleAutoTest::Register(const char* name, Factory factory)
{
    Table().emplace_back(name, factory);
    return true;
}

std::shared_ptr<BattleAutoTest> BattleAutoTest::Create(const char* name, CollisionTestScene& scene)
{
    Factory fallback = nullptr;
    for (const auto& [n, factory] : Table())
    {
        if (std::strcmp(n, name) == 0) return factory(scene);
        if (std::strcmp(n, "feedback") == 0) fallback = factory;
    }
    return fallback ? fallback(scene) : nullptr;
}

void BattleAutoTest::RebuildTerrain()
{
    m_Scene.RebuildTerrain();
}
