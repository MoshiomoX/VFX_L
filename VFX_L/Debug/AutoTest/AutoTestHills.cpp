// ============================================================
// AutoTestHills.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=hills
// VFXL_BATTLE_AUTOTEST=hills：起伏の地面（2026-10-04）。プレイヤーが半径約 14m の円を走り、雑魚 40 体が追う。
// プレイヤーの接地の途切れ・足と地面の差、雑魚の地面からのずれを記録し、草を消した低い視点で "hills look <向き>"
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestHills final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 起伏の地面（VFXL_BATTLE_AUTOTEST=hills、2026-10-04）
// 1 秒: 湧き停止・全消去・無敵・詠唱停止、プレイヤーの周り 10〜25m に雑魚 40 体（倒れない）。
// 1.5〜13.5 秒: カメラ基準の入力を回して円を走る（半径 ≒ 14m）。毎フレーム接地の途切れ（接地 → 空中）と
// 接地中の足と地面（高さ場）の差を数え、0.5 秒毎に "hills t ..."、1 秒毎に雑魚を読み戻して地面からのずれ。
// 13.5 秒に "hills summary"、草を消して低い視点で 4 方向 "hills look n/e/s/w"、草を戻して "hills look grass"、done
// ============================================================
void AutoTestHills::Run(float dt)
{
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();
    const float t = m_AutoTime;
    auto at = [&](float s) { return t >= s && t - dt < s; };
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    const auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
    const float gy = m_Swarm.GetAIParams().groundY;
    static Vector3 s_Start;
    static int s_AirFrames = 0, s_Frames = 0, s_Flips = 0, s_GroundedFrames = 0;
    static bool s_WasGrounded = true;
    static float s_MaxDy = 0.0f, s_SumDy = 0.0f, s_NextLog = 0.0f, s_NextRead = 0.0f;
    static int s_EnemySamples = 0, s_EnemyBad = 0;
    static float s_EnemyMaxDev = 0.0f;
    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    char line[256];

    if (at(1.0f))
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        cam.distance = 9.0f;
        cam.SetPitch(16.0f);
        s_Start = tf.position;
        int spawned = 0;
        for (int i = 0; i < 40; ++i)
        {
            const float a = (float)i / 40.0f * 6.2831853f;
            const float r = 10.0f + (float)(i % 4) * 5.0f;
            const float x = s_Start.x + std::cos(a) * r, z = s_Start.z + std::sin(a) * r;
            int gx, gz;
            m_Grid.WorldToCell({ x, 0.0f, z }, gx, gz);
            if (!m_Grid.IsWalkable(gx, gz)) continue;
            m_Swarm.SpawnEnemy({ x, m_Grid.SampleHeight(x, z) + gy, z }, 100000.0f, 3.5f, Swarm::kEnemyKindMob);
            ++spawned;
        }
        snprintf(line, sizeof(line), "hills start at %.1f,%.1f ground %.2f mobs %d", s_Start.x, s_Start.z,
            m_Grid.SampleHeight(s_Start.x, s_Start.z), spawned);
        AutoTestLog(line);
    }
    if (t < 1.5f) return;

    if (t < 13.5f)
    {
        // カメラ基準の入力の向きを回す = 円を走る（半径 ≒ 速さ / 角速度 = 5 / 0.35 ≒ 14m）
        const float a = (t - 1.5f) * 0.35f;
        pcs.testInput = true;
        pcs.testMove = Vector2(std::sin(a), std::cos(a));
        pcs.testSlide = false;
        pcs.testJump = false;
        ++s_Frames;
        const float feet = tf.position.y - (stats.height * 0.5f + stats.radius);
        const float ground = m_Grid.SampleHeight(tf.position.x, tf.position.z);
        const float dy = feet - ground;
        if (!rb.isGrounded) ++s_AirFrames;
        if (s_WasGrounded && !rb.isGrounded) ++s_Flips;
        s_WasGrounded = rb.isGrounded;
        if (rb.isGrounded)
        {
            ++s_GroundedFrames;
            s_MaxDy = (std::max)(s_MaxDy, std::fabs(dy));
            s_SumDy += std::fabs(dy);
        }
        if (t >= s_NextLog)
        {
            s_NextLog = t + 0.5f;
            const float slope = DirectX::XMConvertToDegrees(std::acos(std::clamp(rb.groundNormal.y, -1.0f, 1.0f)));
            snprintf(line, sizeof(line), "hills t %.1f pos %.1f,%.1f y %.2f ground %.2f dy %.2f grounded %d slope %.0f speed %.2f",
                t, tf.position.x, tf.position.z, feet, ground, dy, (int)rb.isGrounded, slope,
                std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z));
            AutoTestLog(line);
        }
        if (t >= s_NextRead)
        {
            s_NextRead = t + 1.0f;
            std::vector<Swarm::Enemy> es;
            std::vector<uint32_t> st;
            if (m_Swarm.DebugReadEnemies(es, st))
                for (size_t i = 0; i < es.size() && i < st.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    if (es[i].velocity.y < -0.5f) continue;   // 落下中
                    const float dev = es[i].position.y - (m_Grid.SampleHeight(es[i].position.x, es[i].position.z) + gy);
                    ++s_EnemySamples;
                    if (std::fabs(dev) > 0.3f) ++s_EnemyBad;
                    s_EnemyMaxDev = (std::max)(s_EnemyMaxDev, std::fabs(dev));
                }
        }
        return;
    }
    if (at(13.5f))
    {
        pcs.testInput = false;
        snprintf(line, sizeof(line),
            "hills summary frames %d air %d flips %d grounded dy max %.3f mean %.3f | enemies samples %d off>0.3m %d maxDev %.2f",
            s_Frames, s_AirFrames, s_Flips, s_MaxDy, s_GroundedFrames > 0 ? s_SumDy / s_GroundedFrames : 0.0f,
            s_EnemySamples, s_EnemyBad, s_EnemyMaxDev);
        AutoTestLog(line);
        m_Swarm.KillAll();
        m_Grass.GetSettings().enabled = false;
    }
    // 草を消した低い視点で 4 方向（向きを変えてから 0.6 秒後に撮る）
    struct Shot { float time, yaw; const char* name; };
    static const Shot kShots[] = { { 14.3f, 0.0f, "n" }, { 15.5f, 90.0f, "e" }, { 16.7f, 180.0f, "s" }, { 17.9f, 270.0f, "w" } };
    for (const Shot& s : kShots)
    {
        if (at(s.time - 0.6f))
        {
            cam.SetYaw(s.yaw);
            cam.distance = 14.0f;
            cam.SetPitch(5.0f);
            cam.SnapToTarget();
        }
        else if (at(s.time))
        {
            snprintf(line, sizeof(line), "hills look %s", s.name);
            AutoTestLog(line);
        }
    }
    if (at(19.1f))
    {
        m_Grass.GetSettings().enabled = true;
        cam.distance = 9.0f;
        cam.SetPitch(16.0f);
    }
    if (at(19.9f)) AutoTestLog("hills look grass");
    if (at(20.5f)) AutoTestLog("hills done");
}

REGISTER_BATTLE_AUTOTEST("hills", AutoTestHills)
