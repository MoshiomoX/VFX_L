// ============================================================
// BattleAutoTest.cpp
// TEMP-TEST: 戦闘シーンの自動テストの土台（シーンの部品への参照・名前の登録）
// ============================================================
#include "Debug/AutoTest/BattleAutoTest.h"

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
    , m_BossMoundVfx(scene.m_BossMoundVfx)
    , m_ChainHits(scene.m_ChainHits)
    , m_StageElites(scene.m_StageElites)
    , m_EliteHits(scene.m_EliteHits)
    , m_Lifesteal(scene.m_Lifesteal)
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
    , m_GrassFire(scene.m_GrassFire)
    , m_Debris(scene.m_Debris)
    , m_Tumbleweeds(scene.m_Tumbleweeds)
    , m_Dandelions(scene.m_Dandelions)
    , m_GrassShreds(scene.m_GrassShreds)
    , m_AreaVFX(scene.m_AreaVFX)
    , m_VFXContext(scene.m_VFXContext)
    , m_Weather(scene.m_Weather)
    , m_PlayerVFX(scene.m_PlayerVFX)
    , m_SpellFx(scene.m_SpellFx)
    , m_GameUI(scene.m_GameUI)
    , m_Tutorial(scene.m_Tutorial)
    , m_SpellLab(scene.m_SpellLab)
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
    if (!name) name = "";
    Factory feedback = nullptr;
    for (const auto& [n, factory] : Table())
    {
        if (std::strcmp(n, name) == 0) return factory(scene);
        if (std::strcmp(n, "feedback") == 0) feedback = factory;
    }

    // 名前を書かない呼び方（VFXL_BATTLE_AUTOTEST=1 / 空）は昔からの既定 = feedback
    if (name[0] == '\0' || std::strcmp(name, "1") == 0)
        return feedback ? feedback(scene) : nullptr;

    // 登録に無い名前（打ち間違い・切れた名前）は走らせない（2026-10-25。以前は黙って feedback を走らせ、
    // 名前を間違えても「正常な」ログが出来ていた）。autotest.log とコンソールにはっきり書く
    std::string msg = std::string("[Warn] autotest: unknown name \"") + name + "\" - nothing runs. Registered:";
    for (const auto& entry : Table())
        msg += std::string(" ") + entry.first;
    std::cout << msg << std::endl;
    scene.AutoTestLog((std::string("autotest unknown \"") + name + "\" (not registered, nothing runs)").c_str());
    return nullptr;
}

void BattleAutoTest::RebuildTerrain()
{
    m_Scene.RebuildTerrain();
}
