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
    // 名前が空か "1" なら既定の自動テスト（"feedback"：レベルアップ・開箱の反応エフェクト）。
    // 登録に無い名前は null（何も走らせない）を返し、autotest.log とコンソールに警告を 1 行出す
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
    BossIntro&                m_BossIntro;       // 門から呼んだ Boss の登場
    uint32_t&                 m_BossSlamHits;
    uint32_t&                 m_BossMoundVfx;    // 潜砂の砂丘のエフェクトの番号
    uint32_t&                 m_KingRaised;      // 骸の王の大起尸で起こした骸骨の数
    uint32_t&                 m_BeastPetrified;  // 石の守護獣の石化の息で石になった回数
    uint32_t&                 m_BossRageVfx;     // 狂暴の火の粉のエフェクトの番号
    float&                    m_DefeatSlowLeft;  // 倒した瞬間のスローの残り（実時間の秒）
    float&                    m_DefeatWaveR;     // 倒した所から広がる波の半径（< 0 = 無し）
    uint32_t&                 m_ChainHits;       // 鎖の番人の鎖が当たった数
    uint32_t&                 m_ShatterCount;    // 砕氷（凍った敵を氷の範囲が砕いた）の累計
    EliteAttacks&             m_StageElites;     // 面の固定エリート
    uint32_t&                 m_EliteHits;       // エリートの技が当たった数
    LifestealHeal&            m_Lifesteal;       // 吸血の回復
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
    GrassFireField&           m_GrassFire;     // 草の延焼
    SandField&                m_Sand;          // 砂漠の砂の面
    SandFootprints&           m_Footprints;    // 砂漠のプレイヤーの足跡
    MiasmaField&              m_Miasma;        // 遺跡の瘴気
    std::vector<DirectX::SimpleMath::Vector3>& m_Torches;   // 遺跡の松明（miasma の松明の場面）
    GroundDebris&             m_Debris;        // 地面の落ち葉
    WindTrees&                m_WindTrees;     // 風で揺れる木
    WaterSurface&             m_Water;         // 水たまりの水面
    FlowDebugView&            m_FlowDebug;     // F8 の流れの矢印
    Tumbleweeds&              m_Tumbleweeds;   // 転がり草
    DandelionField&           m_Dandelions;    // タンポポ
    GrassShreds&              m_GrassShreds;   // 風源の草屑
    AreaVFXPlayer&            m_AreaVFX;       // CPU のエフェクト（負荷試験で竜巻の見た目を出す）
    VFXContext&               m_VFXContext;
    WeatherSystem&            m_Weather;       // 時刻と天候の出来事
    PlayerVFXSystem&          m_PlayerVFX;     // プレイヤーの動きのエフェクト
    SpellFxSystem&            m_SpellFx;       // 魔法の CPU 側の見た目（アイススパイクのトゲ）
    SandTornadoFx&            m_SandTornadoes; // 砂旋の本体の見た目
    GameUI&                   m_GameUI;
    float&                    m_BatHealTotal;
    GPUParticleSystem&        m_ParticleSystem;   // ソフトパーティクル・ビルボードの霧の切り替え
    CloudShadows&             m_Clouds;           // 雲の影
    TutorialGuide&            m_Tutorial;      // 初回のチュートリアル
    SpellLab&                 m_SpellLab;      // 実験場（トレーニング）
    TerrainGenerator::Config& m_TerrainConfig;
    TerrainGenerator::Layout& m_TerrainLayout;
    MapData::Map&             m_TerrainMap;    // 建てた物の記録
    std::string&              m_MapFile;       // 読む地図の名前（空 = 生成）
    std::vector<Entity>&      m_Terrain;       // 地形の実体
    int&                      m_StageIndex;
    std::vector<uint8_t>&     m_GrassMask;     // 草を生やすマス（オアシスの確認）
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
