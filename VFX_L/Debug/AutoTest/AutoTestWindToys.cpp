// ============================================================
// AutoTestWindToys.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=windtoys（2026-10-22。風に流される物：転がり草・タンポポ・マントの局所の風）
//   砂漠（VFXL_STAGE=2）で転がり草、草原（VFXL_STAGE=1）でタンポポを見る。
//   1 秒: 湧き停止・全消し・HP 10 万・MP 無限・入力 0。9×9 に枠を敷き、竜巻 (3,4) + 渦 (4,2) + 突風 (4,5)（twister と同じ並び）、
//         正面 +Z 11〜15m の横 12m に雑魚 18 体（HP 300、1.8 m/s）。転がり草は正面 11m の点の周り 5〜16m に置き直す。
//         カメラは斜め横 22m / 25°（毎フレーム固定）。
//   0.5 秒ごとに `windtoys t twisters tw n air maxUp avgSpd dl full bare flying cape back up`
//   （tw = 転がり草：数・浮いている数・地面からの最高 m・平均の速さ、dl = タンポポ：玉の付いた本数・ほどけた本数・飛んでいる綿毛、
//    cape = マントの先が根からどれだけ後ろ / 上へ離れているか m）。
//   最初の竜巻が出てから 0.8 / 2.0 / 3.2 / 6.0 秒で `windtoys look twister1〜3 / after`。
//   その後 魔法を外して 2.5 秒横へ走る（タンポポを踏む）、1.5 秒で `windtoys look walk`、最後に `windtoys result` + done
//   VFXL_WINDTOYS_SHOT=1（2026-10-23、作品紹介資料の写真用）：HP / MP は普段の上限のまま毎フレーム満タン + 無敵
//   （HUD に 100000 などが出ない）、タンポポは入れない、カメラは後ろ寄り 13m / 20° で竜巻の通る正面を写す
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    constexpr float kCamYaw = 50.0f;
    constexpr float kCamPitch = 25.0f;
    // VFXL_WINDTOYS_SHOT=1 の時のカメラ（資料の写真用）
    constexpr float kShotYaw = 18.0f;
    constexpr float kShotPitch = 20.0f;
    constexpr float kShotDistance = 13.0f;

    bool ShotMode()
    {
        static const bool on = []
            {
                char v[8] = {};
                return GetEnvironmentVariableA("VFXL_WINDTOYS_SHOT", v, sizeof(v)) > 0 && v[0] == '1';
            }();
        return on;
    }

    class AutoTestWindToys final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void LogStats();
        float m_NextRead = 1.5f;
        float m_FirstTwister = -1.0f;
        int m_MaxAir = 0;
        float m_MaxUp = 0.0f;
        int m_MaxFlying = 0;
        int m_BareBeforeWalk = -1;
        int m_LastBare = 0;
        float m_MaxLocalAtPlayer = 0.0f;
        // マントの先（毎フレーム。突風は 0.8 秒しか吹かないので 0.5 秒毎の記録では山を逃す）。
        // windy = プレイヤーの所の局所の風が 3 m/s を越えているフレーム、calm = それ以外（走り出す前まで）
        int m_WindyFrames = 0;
        float m_CapeDevWindy = 0.0f, m_CapeDevCalm = 0.0f;   // 根から先へのベクトルが静止の形からずれた最大 m
        Vector3 m_CapeRest;
        bool m_HasCapeRest = false;
        bool CapeTip(float& back, float& side, float& up, Vector3* raw = nullptr);
    };
}

// マントの先：根から後ろ（体の向きの逆）/ 横 / 上へ m
bool AutoTestWindToys::CapeTip(float& back, float& side, float& up, Vector3* raw)
{
    back = side = up = 0.0f;
    if (!m_Registry.Has<ClothChainComponent>(m_Player) || !m_Registry.Has<TransformComponent>(m_Player)) return false;
    const auto& c = m_Registry.Get<ClothChainComponent>(m_Player);
    if (!c.ready || (int)c.pos.size() != c.cols * c.rows) return false;
    const float yaw = DirectX::XMConvertToRadians(m_Registry.Get<TransformComponent>(m_Player).rotation.y);
    const Vector3 fwd(std::sin(yaw), 0.0f, std::cos(yaw));
    const Vector3 right(fwd.z, 0.0f, -fwd.x);
    const Vector3 d = c.CenterTip() - c.CenterRoot();
    back = -d.Dot(fwd);
    side = d.Dot(right);
    up = d.y;
    if (raw) *raw = d;
    return true;
}

void AutoTestWindToys::LogStats()
{
    char line[400];
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    int air = 0;
    float maxUp = 0.0f, spd = 0.0f;
    const int n = m_Tumbleweeds.Count();
    for (int i = 0; i < n; ++i)
    {
        Vector3 p, v;
        bool a = false;
        if (!m_Tumbleweeds.GetDebug(i, p, v, a)) continue;
        if (a) ++air;
        maxUp = (std::max)(maxUp, p.y - m_Grid.SampleHeight(p.x, p.z));
        spd += std::sqrt(v.x * v.x + v.z * v.z);
    }
    if (n > 0) spd /= (float)n;
    m_MaxAir = (std::max)(m_MaxAir, air);
    m_MaxUp = (std::max)(m_MaxUp, maxUp);

    int full = 0, bare = 0, flying = 0, nearTwister = -1;
    Vector3 tp;
    if (m_WeaponSystem.GetTwisterPos(0, tp)) m_Dandelions.DebugStats(full, bare, flying, &tp, 6.5f, &nearTwister);
    else m_Dandelions.DebugStats(full, bare, flying);
    m_MaxFlying = (std::max)(m_MaxFlying, flying);
    m_LastBare = bare;

    float back = 0.0f, up = 0.0f, side = 0.0f;
    CapeTip(back, side, up);
    // CPU に読み戻した風源の表：使っている枠の数と、プレイヤーの所の空気の速さ
    const LocalWind& lw = m_Swarm.GetLocalWind();
    int sources = 0;
    for (const auto& w : lw.sources) if (w.radius > 0.0f) ++sources;
    const Vector3& pp = tf.position;
    const float localAtPlayer = lw.Air(pp.x, pp.z).Length();
    snprintf(line, sizeof(line), "windtoys t %.2f twisters %d tw %d air %d maxUp %.2f avgSpd %.2f dl full %d bare %d flying %d nearTwister %d cape back %.2f side %.2f up %.2f sources %d localAtPlayer %.2f",
        m_AutoTime, m_WeaponSystem.GetTwisterCount(), n, air, maxUp, spd, full, bare, flying, nearTwister, back, side, up, sources, localAtPlayer);
    AutoTestLog(line);
}

void AutoTestWindToys::Run()
{
    char line[300];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player) && !ShotMode())
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
        }
        if (m_Registry.Has<ManaComponent>(m_Player) && !ShotMode())
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            FillAllFrames(bp);
            BackpackLogic::Place(bp, ItemID::Twister, 3, 4, 0);
            BackpackLogic::Place(bp, ItemID::VoidVortex, 4, 2, 0);
            BackpackLogic::Place(bp, ItemID::Gust, 4, 5, 0);
            bp.dirty = true;
        }
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int k = 0; k < 18; ++k)
        {
            const float x = tf.position.x - 6.0f + 12.0f * (float)(k % 6) / 5.0f;
            const float z = tf.position.z + 11.0f + 2.0f * (float)(k / 6);
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 300.0f, 1.8f, Swarm::kEnemyKindMob);
        }
        // 転がり草は竜巻が通る辺り（正面 11m）の周りへ
        auto& ts = m_Tumbleweeds.GetSettings();
        ts.spawnMin = 5.0f;
        ts.spawnMax = 16.0f;
        m_Tumbleweeds.Reset(m_Registry, m_Grid, m_TerrainConfig.biome, m_TerrainConfig.seed, tf.position + Vector3(0.0f, 0.0f, 11.0f));
        // タンポポは既定で切（2026-10-23）。このテストでは入れて見る（写真用では入れない）
        m_Dandelions.GetSettings().enabled = !ShotMode();
        // VFXL_WINDTOYS_FORCE=1：天候の風でほどける閾値を下げる（ほどける流れそのものが動くかの確認）
        char force[8] = {};
        if (GetEnvironmentVariableA("VFXL_WINDTOYS_FORCE", force, sizeof(force)) > 0 && force[0] == '1')
            m_Dandelions.GetSettings().releaseWind = 0.3f;
        // VFXL_WINDTOYS_NOCAPEWIND=1：マントに局所の風を当てない（効いているかの見比べ）
        char noCape[8] = {};
        if (GetEnvironmentVariableA("VFXL_WINDTOYS_NOCAPEWIND", noCape, sizeof(noCape)) > 0 && noCape[0] == '1'
            && m_Registry.Has<ClothChainComponent>(m_Player))
            m_Registry.Get<ClothChainComponent>(m_Player).localWindMul = 0.0f;

        auto& cam = m_Camera.Camera();
        cam.SetYaw(ShotMode() ? kShotYaw : kCamYaw);
        cam.SetPitch(ShotMode() ? kShotPitch : kCamPitch);
        cam.distance = ShotMode() ? kShotDistance : 22.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "windtoys start stage %d biome %d tumbleweeds %d", m_StageIndex, (int)m_TerrainConfig.biome, m_Tumbleweeds.Count());
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;
    {
        auto& cam = m_Camera.Camera();
        cam.SetYaw(ShotMode() ? kShotYaw : kCamYaw);
        cam.SetPitch(ShotMode() ? kShotPitch : kCamPitch);
    }
    if (ShotMode())
    {
        // HUD に普段どおりの数字が出るよう、上限はそのままで満タンを保つ
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.invincible = true;
            hp.current = hp.max;
        }
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.current = mp.max;
        }
    }

    if (m_AutoTime >= m_NextRead)
    {
        m_NextRead += 0.5f;
        LogStats();
    }
    // マント：毎フレーム、静止の形（最初の 1.5 秒の終わり）からのずれを局所の風の有無で分けて最大を取る（走り出す前まで）
    if (m_AutoStep < 6)
    {
        float back, side, up;
        Vector3 d;
        if (CapeTip(back, side, up, &d))
        {
            if (!m_HasCapeRest && m_AutoTime >= 1.5f) { m_CapeRest = d; m_HasCapeRest = true; }
            if (m_HasCapeRest)
            {
                const float dev = (d - m_CapeRest).Length();
                const float la = m_Swarm.GetLocalWind().Air(tf.position.x, tf.position.z).Length();
                m_MaxLocalAtPlayer = (std::max)(m_MaxLocalAtPlayer, la);
                if (la > 3.0f) { ++m_WindyFrames; m_CapeDevWindy = (std::max)(m_CapeDevWindy, dev); }
                else m_CapeDevCalm = (std::max)(m_CapeDevCalm, dev);
            }
        }
    }
    if (m_AutoStep == 1 && m_AutoTime >= 1.8f) { AutoTestLog("windtoys look start"); m_AutoStep = 2; }
    if (m_FirstTwister < 0.0f && m_WeaponSystem.GetTwisterCount() > 0)
    {
        m_FirstTwister = m_AutoTime;
        // 竜巻の出た所の周り 2〜6.5m に転がり草を置き直す（吸い込み・巻き上げを直接見る）
        Vector3 tp;
        if (m_WeaponSystem.GetTwisterPos(0, tp))
        {
            auto& ts = m_Tumbleweeds.GetSettings();
            ts.spawnMin = 2.0f;
            ts.spawnMax = 6.5f;
            m_Tumbleweeds.Reset(m_Registry, m_Grid, m_TerrainConfig.biome, m_TerrainConfig.seed, tp);
            snprintf(line, sizeof(line), "windtoys twister at %.1f %.1f, tumbleweeds placed %d", tp.x, tp.z, m_Tumbleweeds.Count());
            AutoTestLog(line);
        }
    }
    if (m_FirstTwister < 0.0f) return;

    const float t = m_AutoTime - m_FirstTwister;
    if (m_AutoStep == 2 && t >= 0.8f) { AutoTestLog("windtoys look twister1"); m_AutoStep = 3; }
    if (m_AutoStep == 3 && t >= 2.0f) { AutoTestLog("windtoys look twister2"); m_AutoStep = 4; }
    if (m_AutoStep == 4 && t >= 3.2f) { AutoTestLog("windtoys look twister3"); m_AutoStep = 5; }
    if (m_AutoStep == 5 && t >= 6.0f)
    {
        AutoTestLog("windtoys look after");
        // 魔法を外して前へ走る（タンポポを踏む）
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            bp.dirty = true;
        }
        m_BareBeforeWalk = m_LastBare;
        m_PlayerControlSystem.testMove = Vector2(1.0f, 0.0f);   // 横へ（前は竜巻が通って綿毛が残っていない）
        m_AutoStep = 6;
    }
    if (m_AutoStep == 6 && t >= 7.5f) { AutoTestLog("windtoys look walk"); m_AutoStep = 7; }
    if (m_AutoStep == 7 && t >= 8.5f)
    {
        m_PlayerControlSystem.testMove = Vector2::Zero;
        LogStats();
        snprintf(line, sizeof(line), "windtoys result firstTwister %.2f maxAir %d maxUp %.2f maxFlying %d bareBeforeWalk %d bareAfterWalk %d maxLocalAtPlayer %.2f windyFrames %d capeDevWindy %.2f capeDevCalm %.2f",
            m_FirstTwister, m_MaxAir, m_MaxUp, m_MaxFlying, m_BareBeforeWalk, m_LastBare, m_MaxLocalAtPlayer, m_WindyFrames, m_CapeDevWindy, m_CapeDevCalm);
        AutoTestLog(line);
        AutoTestLog("windtoys done");
        m_AutoStep = 8;
    }
}

REGISTER_BATTLE_AUTOTEST("windtoys", AutoTestWindToys)
