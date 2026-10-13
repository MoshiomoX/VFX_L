// ============================================================
// AutoTestGust.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=gust
// 突風（基本の範囲魔法、2026-10-12）：吹き飛ばしの距離（雑魚 / エリート）と見た目（草が倒れる・土煙の輪）。
//   1 秒: 湧き停止・全消し・無敵・MP 無限・入力 0。バックパックに突風だけ。周り 3m の輪に動かない雑魚 7 体 + エリート 1 体（HP 10 万）。
//   以後 0.25 秒ごとに回読して `gust t casts mobAvg eliteDist`（プレイヤーからの水平距離の平均 / エリートの距離）。
//   最初の発動の 0.1 / 0.24 / 0.42 秒後に `gust look flat / mid / ring`（風の見た目の 3 コマ）。6 秒 done。
// 期待：発動後 0.5 秒で雑魚の平均距離が約 3m → 6m 以上、エリートはその半分くらい
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestGust final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float m_NextRead = 1.02f;   // 最初の発動（1.0 秒過ぎ）の直前にも 1 回読む
        float m_FirstCast = -1.0f;
        int   m_Casts = 0;
    };
}

void AutoTestGust::Run()
{
    char line[400];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Gust, lo + 1, lo, 0);   // 横 2 マス
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 標的のリードバック（2〜3 フレーム）を待ってから撃つ。待たないと「標的無し = 自分の周りの円盤」で全員飛ぶ
        }
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int k = 0; k < 8; ++k)
        {
            const float ang = (float)k / 8.0f * 6.2831853f;
            const float x = tf.position.x + std::sin(ang) * 3.0f, z = tf.position.z + std::cos(ang) * 3.0f;
            const uint32_t kind = (k == 0) ? Swarm::kEnemyKindElite : Swarm::kEnemyKindMob;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 0.0f, kind);
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(38.0f);
        cam.distance = 11.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("gust start");
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (m_AutoStep == 1 && m_FirstCast < 0.0f && m_AutoTime >= 1.6f && m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;

    if (!m_WeaponSystem.GetGustFx().empty())
    {
        m_Casts += (int)m_WeaponSystem.GetGustFx().size();
        if (m_FirstCast < 0.0f) m_FirstCast = m_AutoTime;
    }

    if (m_AutoTime >= m_NextRead)
    {
        m_NextRead += 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        if (m_Swarm.DebugReadEnemies(en, st, &ex))
        {
            float mobSum = 0.0f, elite = 0.0f;
            int mobs = 0;
            std::string each;   // 1 体ずつ「角度:距離」（前へ吹く扇の中だけ飛ぶかを見る。2026-10-13）
            for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead) continue;
                const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (ex[i].kind == Swarm::kEnemyKindElite) elite = d;
                else { mobSum += d; ++mobs; }
                char one[32];
                snprintf(one, sizeof(one), " %.0f:%.1f", std::atan2(dx, dz) * 57.2958f, d);
                each += one;
            }
            snprintf(line, sizeof(line), "gust t %.2f casts %d mobAvg %.2f eliteDist %.2f each%s", m_AutoTime, m_Casts,
                mobs ? mobSum / mobs : 0.0f, elite, each.c_str());
            AutoTestLog(line);
        }
    }

    if (m_AutoStep == 1 && m_FirstCast >= 0.0f && m_AutoTime >= m_FirstCast + 0.1f) { AutoTestLog("gust look flat"); m_AutoStep = 2; }
    if (m_AutoStep == 2 && m_AutoTime >= m_FirstCast + 0.24f) { AutoTestLog("gust look mid"); m_AutoStep = 5; }
    if (m_AutoStep == 5 && m_AutoTime >= m_FirstCast + 0.42f) { AutoTestLog("gust look ring"); m_AutoStep = 3; }
    if (m_AutoStep == 3 && m_AutoTime >= 6.0f) { AutoTestLog("gust done"); m_AutoStep = 4; }
}

REGISTER_BATTLE_AUTOTEST("gust", AutoTestGust)
