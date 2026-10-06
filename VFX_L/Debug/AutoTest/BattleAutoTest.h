// ============================================================
// BattleAutoTest.h
// TEMP-TEST: 戦闘シーンの自動テスト（環境変数 VFXL_BATTLE_AUTOTEST=<名前>）の土台。
//
// 自動テストは 1 本 1 ファイル（Debug/AutoTest/AutoTest<名前>.cpp）。このクラスを継いで
// UpdateGameplay か UpdateFrame を書き、末尾で REGISTER_BATTLE_AUTOTEST("名前", クラス) する。
// シーン（CollisionTestScene）は名前で 1 つ作って毎フレーム呼ぶだけで、個々の自動テストは知らない。
//
// シーンの部品へは下の参照（シーンのメンバと同じ名前）で触る。足りない物はここへ足す
// ============================================================
#pragma once
#include "Scene/CollisionTestScene.h"
#include <memory>

class BattleAutoTest
{
public:
    explicit BattleAutoTest(CollisionTestScene& scene);
    virtual ~BattleAutoTest() = default;

    // gameplay の最後（UpdateGameplay）から。バックパック・一時停止・選択の画面が開いている間は呼ばれない
    virtual void UpdateGameplay(float) {}
    // シーンの Update から毎フレーム（gameplay が止まる画面を開いたまま進める自動テスト用）
    virtual void UpdateFrame(float) {}
    // true = 経過時間（m_AutoTime）を自分で進める。false ならシーンが gameplay の中で進める
    virtual bool SelfClock() const { return false; }

    // ---- 名前 → 自動テスト ----
    using Factory = std::shared_ptr<BattleAutoTest>(*)(CollisionTestScene&);
    static bool Register(const char* name, Factory factory);
    // 名前が登録に無ければ既定の自動テスト（"feedback"：レベルアップ・開箱の反応エフェクト）
    static std::shared_ptr<BattleAutoTest> Create(const char* name, CollisionTestScene& scene);

protected:
    // 実時間（ms）付きで autotest.log へ 1 行
    void AutoTestLog(const char* what);
    // 野原の置物（木・石・低木）の表示。outCount = 登録数
    void SetDecorPropsVisible(bool visible, int* outCount);
    // 地形の建て直し（m_MapFile があれば保存した地図から、無ければ seed から）
    void RebuildTerrain();

    // ---- 自動テストの進行（シーンが持つ。AutoTestLog が経過時間を書くので）----
    int&   m_AutoStep;
    float& m_AutoTime;
    bool&  m_AutoInteract;   // true にすると次のフレームに F を押した扱い

    // ---- シーンの部品 ----
    Registry&                 m_Registry;
    Entity&                   m_Player;
    BattleCamera&             m_Camera;
    GridWorld&                m_Grid;
    SwarmSystem&              m_Swarm;
    MobSpawner&               m_Mobs;
    StageDirector&            m_Stage;
    BossAttacks&              m_BossAttacks;
    uint32_t&                 m_BossSlamHits;
    CollisionSystem&          m_CollisionSystem;
    PlayerControlSystem&      m_PlayerControlSystem;
    PlayerAnimSystem&         m_PlayerAnimSystem;
    WeaponSystem&             m_WeaponSystem;
    LevelUpSystem&            m_LevelUpSystem;
    RewardCrateSystem&        m_Crates;
    PickupSystem&             m_Pickups;
    BattleAudio&              m_Audio;
    Outline&                  m_Outline;
    StressTestTools&          m_Stress;
    StaticPropRenderer&       m_StaticProps;
    GrassRenderer&            m_Grass;
    WeatherSystem&            m_Weather;       // 時刻と天候の出来事
    GameUI&                   m_GameUI;
    TerrainGenerator::Config& m_TerrainConfig;
    TerrainGenerator::Layout& m_TerrainLayout;
    MapData::Map&             m_TerrainMap;    // 建てた物の記録
    std::string&              m_MapFile;       // 読む地図の名前（空 = 生成）
    std::vector<Entity>&      m_Terrain;       // 地形の実体
    int&                      m_StageIndex;
    uint32_t&                 m_PortalVfx;
    float&                    m_RunTime;
    float&                    m_ExpGained;
    float&                    m_ScreenW;
    float&                    m_ScreenH;
    bool&                     m_ShowWireframe;
    bool&                     m_ShowWandDebug;
    bool&                     m_ShowGridDebug;

private:
    CollisionTestScene& m_Scene;
};

// 自動テストのファイルの末尾に 1 行。名前は VFXL_BATTLE_AUTOTEST の値
#define REGISTER_BATTLE_AUTOTEST(name, Class) \
    namespace { const bool s_Registered_##Class = BattleAutoTest::Register(name, \
        [](CollisionTestScene& scene) -> std::shared_ptr<BattleAutoTest> { return std::make_shared<Class>(scene); }); }
