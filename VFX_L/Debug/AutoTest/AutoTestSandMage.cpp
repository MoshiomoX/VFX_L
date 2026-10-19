// ============================================================
// AutoTestSandMage.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=sandmage
// 砂嵐の術師（kEnemyKindSandMage。2026-10-17）：正面 12m に 1 体。距離を取って立ち止まり、溜めてから砂旋
// （kEnemyKindTornado = 敵の弾を敵の 1 種類で作った物）をプレイヤーへ放つ。砂旋に触れたプレイヤーは高く吹き飛ぶ
// （PlayerHitInfo の wind → KnockKind::Wind）。ユーザー 10-17「能把玩家吹飞的」→ 四案から「飛ぶ砂旋」
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestSandMage final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・HP 10 万・詠唱停止。正面 +Z 12m に術師 1 体、カメラ背後 11m / 24°
// 0.25 秒毎に `sandmage t fuse dist tornadoes tdist tfuse lift hits`（fuse > 0 = 溜め中、< 0 = クールダウン；
//   tdist = 一番近い砂旋までの距離、tfuse = その残り秒、lift = プレイヤーが立っていた高さからの上昇）。
//   溜めが始まったら `look windup`、砂旋を初めて見た 0.5 秒後に `look tornado`、初めて 0.8m 以上浮いたら `look hit`。
// 7 秒: 詠唱を戻す。11 秒: `sandmage fight alive hits tornadoes maxLift` + `look fight`。12 秒: done
// ============================================================
void AutoTestSandMage::Run()
{
    char line[240];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    static float s_NextLog = 1.0f, s_LastHp = 0.0f, s_BaseY = 0.0f, s_MaxLift = 0.0f, s_FirstTornadoAt = -1.0f;
    static int s_Hits = 0, s_TornadoesSeen = 0;
    static bool s_LookWindup = false, s_LookTornado = false, s_LookHit = false;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
            s_LastHp = hp.current;
        }
        s_BaseY = tf.position.y;
        const float groundY = m_Swarm.GetAIParams().groundY;
        float hp = 15.0f, speed = 3.5f;
        m_Mobs.KindStats(Swarm::kEnemyKindSandMage, hp, speed);
        const float x = tf.position.x, z = tf.position.z + 12.0f;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, Swarm::kEnemyKindSandMage);
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(24.0f);
        cam.distance = 11.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "sandmage start hp %.0f speed %.2f tornadoSpeed %.2f", hp, speed, m_Mobs.tornadoSpeed);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f) ++s_Hits;
        s_LastHp = hp.current;
    }
    const float lift = tf.position.y - s_BaseY;
    s_MaxLift = (std::max)(s_MaxLift, lift);
    if (!s_LookHit && lift > 0.8f) { AutoTestLog("sandmage look hit"); s_LookHit = true; }

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog += 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        if (m_Swarm.DebugReadEnemies(en, st, &ex))
        {
            float fuse = 0.0f, dist = 0.0f, tdist = 1e9f, tfuse = 0.0f;
            int tornadoes = 0;
            bool mage = false;
            for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead) continue;
                const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (ex[i].kind == Swarm::kEnemyKindSandMage && !mage) { mage = true; fuse = ex[i].fuse; dist = d; }
                else if (ex[i].kind == Swarm::kEnemyKindTornado)
                {
                    ++tornadoes;
                    if (d < tdist) { tdist = d; tfuse = ex[i].fuse; }
                }
            }
            if (tornadoes > 0 && s_FirstTornadoAt < 0.0f) s_FirstTornadoAt = m_AutoTime;
            s_TornadoesSeen = (std::max)(s_TornadoesSeen, (int)m_Mobs.GetTornadoesSpawned());
            snprintf(line, sizeof(line), "sandmage t %.2f fuse %.2f dist %.2f tornadoes %d tdist %.2f tfuse %.2f lift %.2f hits %d",
                m_AutoTime, fuse, dist, tornadoes, tornadoes > 0 ? tdist : -1.0f, tfuse, lift, s_Hits);
            AutoTestLog(line);
            if (!s_LookWindup && mage && fuse > 0.3f) { AutoTestLog("sandmage look windup"); s_LookWindup = true; }
        }
    }
    if (!s_LookTornado && s_FirstTornadoAt >= 0.0f && m_AutoTime >= s_FirstTornadoAt + 0.5f) { AutoTestLog("sandmage look tornado"); s_LookTornado = true; }
    if (m_AutoStep == 1 && m_AutoTime >= 7.0f)
    {
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        m_AutoStep = 2;
    }
    if (m_AutoStep == 2 && m_AutoTime >= 11.0f)
    {
        snprintf(line, sizeof(line), "sandmage fight alive %u hits %d tornadoes %u maxLift %.2f",
            m_Swarm.GetCounters().aliveEnemies, s_Hits, m_Mobs.GetTornadoesSpawned(), s_MaxLift);
        AutoTestLog(line);
        AutoTestLog("sandmage look fight");
        m_AutoStep = 3;
    }
    if (m_AutoStep == 3 && m_AutoTime >= 12.0f) { AutoTestLog("sandmage done"); m_AutoStep = 4; }
}

REGISTER_BATTLE_AUTOTEST("sandmage", AutoTestSandMage)
