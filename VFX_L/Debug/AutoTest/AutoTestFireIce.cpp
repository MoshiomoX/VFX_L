// ============================================================
// AutoTestFireIce.cpp
// TEMP-TEST: 枯れ草の帯の火と氷（2026-11-01、ユーザー：無限に燃えるので本編に出ていない）
// VFXL_BATTLE_AUTOTEST=fireice
//   A: 帯を 1 つ点ける → 燃えるマスが 0 へ戻る時刻 / 焦げたマス。すぐ点け直す（燃えない筈）→ 40 秒後に点け直す（燃える筈）
//   B: 別の帯に動かない雑魚（帯の中 6 体 + 帯の外 4 体）。点けて 12 秒 → 帯の中だけ HP が減る
//   C: 別の帯を点け、3 秒後に凍らせる範囲を重ねる → 燃えるマスが 0 へ
//   D: 池に凍らせる範囲 → 凍った texel 数、氷の上 / 水の中を歩く雑魚の速さ
//   E: 池の真ん中に立って凍らせる → 足が氷の高さ・水しぶき無し・跳べる・融けたら池の底へ
// VFXL_BATTLE_AUTOTEST=firegame
//   普通に湧かせて火球 + メテオを撃ち続ける 2 分（VFXL_FIREGAME_SEC）。10 秒毎に 燃えるマス / 焦げたマス / 撃破 / fps
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    constexpr float kPatchRadius = 4.0f;   // 帯の中心を選ぶ時に全部枯れ草であって欲しい半径

    class AutoTestFireIce final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        bool PatchAt(float x, float z) const;
        bool FindPatches();
        void LookAt(const Vector3& p);
        void Read(const char* tag);
        void IceArea(const Vector3& p, float radius);
        bool FindPond();
        void SpawnWalkers();
        void Speeds(float& sum, int& n);
        void LogFeet(const char* tag);
        uint32_t Burning() const { uint32_t b = 0, c = 0; m_GrassFire.DebugReadStats(b, c); return b; }

        Vector3 m_Patch[3];
        float m_Next = 0.0f, m_T0 = 0.0f;
        uint32_t m_Peak = 0, m_Charred = 0;
        float m_LookAt = 0.0f;
        Vector3 m_Pond = Vector3::Zero, m_Across = Vector3::Zero;
        float m_SpeedSum = 0.0f;
        int m_SpeedN = 0;
    };

    class AutoTestFireGame final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
        float m_Next = 10.0f, m_Span = 0.0f, m_End = 120.0f;
        int m_Frames = 0;
        uint32_t m_PeakBurn = 0, m_LastKills = 0;
    };
}

bool AutoTestFireIce::PatchAt(float x, float z) const
{
    if (!m_GrassFire.IsDry(x, z)) return false;
    for (int k = 0; k < 8; ++k)
    {
        const float a = 0.7853982f * (float)k;
        const float px = x + std::sin(a) * kPatchRadius, pz = z + std::cos(a) * kPatchRadius;
        if (!m_GrassFire.IsDry(px, pz)) return false;
        int gx, gz;
        m_Grid.WorldToCell(Vector3(px, 0.0f, pz), gx, gz);
        if (!m_Grid.IsDryWalkable(gx, gz)) return false;
    }
    return true;
}

bool AutoTestFireIce::FindPatches()
{
    const Vector3 from = m_Registry.Get<TransformComponent>(m_Player).position;
    int found = 0;
    for (float r = 8.0f; r < 90.0f && found < 3; r += 2.0f)
        for (int k = 0; k < 24 && found < 3; ++k)
        {
            const float a = 6.2831853f * (float)k / 24.0f;
            const float x = from.x + std::sin(a) * r, z = from.z + std::cos(a) * r;
            if (!PatchAt(x, z)) continue;
            bool farEnough = true;
            for (int i = 0; i < found; ++i)
                if ((Vector2(x, z) - Vector2(m_Patch[i].x, m_Patch[i].z)).Length() < 28.0f) farEnough = false;
            if (!farEnough) continue;
            m_Patch[found++] = Vector3(x, m_Grid.SampleHeight(x, z), z);
        }
    return found == 3;
}

void AutoTestFireIce::LookAt(const Vector3& p)
{
    const Vector3 v(p.x, p.y + 0.9f, p.z);
    m_Camera.SetTargetOverride(&v);
    m_Camera.Camera().SnapToTarget();
}

void AutoTestFireIce::IceArea(const Vector3& p, float radius)
{
    Swarm::Area a;
    a.center = p;
    a.radius = radius;
    a.halfHeight = 4.0f;
    a.damage = 0.0f;
    a.tickInterval = 1.0f;
    a.timeLeft = 0.1f;
    a.flags = 6u << Swarm::kAreaFreezeShift;
    m_Swarm.SpawnArea(a);
}

// 一番広い池。プレイヤーはその向こう岸に立ち、雑魚は反対の岸から池を渡って来る
bool AutoTestFireIce::FindPond()
{
    const float kCs = GridWorld::kCellSize;
    int best = -1;
    for (int gz = 2; gz < m_Grid.Depth() - 2; ++gz)
        for (int gx = 2; gx < m_Grid.Width() - 2; ++gx)
        {
            if (!m_Grid.IsWater(gx, gz)) continue;
            int around = 0;
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                    around += m_Grid.IsWater(gx + dx, gz + dz) ? 1 : 0;
            if (around <= best) continue;
            const float x = m_Grid.OriginX() + (gx + 0.5f) * kCs, z = m_Grid.OriginZ() + (gz + 0.5f) * kCs;
            float wy = 0.0f;
            if (!m_Water.WaterAt(x, z, wy)) continue;
            best = around;
            m_Pond = Vector3(x, wy, z);
        }
    return best >= 0;
}

void AutoTestFireIce::SpawnWalkers()
{
    const float gy = m_Swarm.GetAIParams().groundY;
    for (int k = 0; k < 6; ++k)
    {
        const float x = m_Pond.x - 7.0f, z = m_Pond.z - 2.5f + (float)k;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 100000.0f, 3.5f, Swarm::kEnemyKindMob);
    }
}

// 池のマスの上の雑魚の速さを足す
void AutoTestFireIce::Speeds(float& sum, int& n)
{
    std::vector<Swarm::Enemy> en; std::vector<uint32_t> st;
    if (!m_Swarm.DebugReadEnemies(en, st)) return;
    for (size_t i = 0; i < en.size(); ++i)
    {
        if (st[i] == Swarm::kStateDead) continue;
        int gx, gz;
        m_Grid.WorldToCell(en[i].position, gx, gz);
        if (!m_Grid.IsWater(gx, gz)) continue;
        const float vx = en[i].velocity.x, vz = en[i].velocity.z;
        sum += std::sqrt(vx * vx + vz * vz);
        ++n;
    }
}

// 足と水面の差（+ = 水面より上、池の底なら約 -0.35）、接地、水の判定
void AutoTestFireIce::LogFeet(const char* tag)
{
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
    float surface = 0.0f;
    m_Water.WaterAt(tf.position.x, tf.position.z, surface);
    char line[160];
    snprintf(line, sizeof(line), "fireice %s feetToSurface %.2f grounded %d inWater %d onIce %d vy %.1f", tag,
        tf.position.y - (stats.height * 0.5f + stats.radius) - surface, m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded ? 1 : 0,
        m_PlayerVFX.InWater() ? 1 : 0, m_PlayerVFX.OnIce() ? 1 : 0, m_Registry.Get<RigidbodyComponent>(m_Player).velocity.y);
    AutoTestLog(line);
}

void AutoTestFireIce::Read(const char* tag)
{
    uint32_t b = 0, c = 0;
    m_GrassFire.DebugReadStats(b, c);
    m_Peak = (std::max)(m_Peak, b);
    m_Charred = (std::max)(m_Charred, c);
    char line[120];
    snprintf(line, sizeof(line), "fireice %s t %.1f burning %u charred %u", tag, m_AutoTime - m_T0, b, c);
    AutoTestLog(line);
}

void AutoTestFireIce::Run()
{
    char line[200];
    const float t = m_AutoTime - m_T0;
    switch (m_AutoStep)
    {
    case 0:
        if (m_AutoTime < 1.0f) return;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Weather.eventsEnabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        m_GameUI.hideAll = true;
        {
            auto& cam = m_Camera.Camera();
            cam.distance = 22.0f;
            cam.SetPitch(38.0f);
            cam.avoidOcclusion = false;
        }
        if (!FindPatches())
        {
            // 遺跡は草が無い：火は飛ばして池だけ
            AutoTestLog("fireice no dry patches");
            m_T0 = m_AutoTime;
            m_AutoStep = 10;
            return;
        }
        snprintf(line, sizeof(line), "fireice patches (%.0f,%.0f) (%.0f,%.0f) (%.0f,%.0f)",
            m_Patch[0].x, m_Patch[0].z, m_Patch[1].x, m_Patch[1].z, m_Patch[2].x, m_Patch[2].z);
        AutoTestLog(line);
        LookAt(m_Patch[0]);
        m_T0 = m_AutoTime;
        m_Next = 1.0f;
        m_AutoStep = 1;
        break;

    // ---- A: 燃え尽きて止まる ----
    case 1:
        if (t >= 0.3f && m_LookAt == 0.0f) { AutoTestLog("fireice look A_dry"); m_LookAt = -1.0f; }   // 点ける前（点けた瞬間は熱で光る）
        if (t < 0.8f) return;
        m_GrassFire.Ignite(m_Patch[0], 1.5f);
        m_T0 = m_AutoTime;
        m_Next = 1.0f;
        m_Peak = 0;
        m_LookAt = 5.0f;
        m_AutoStep = 2;
        break;
    case 2:
        if (m_LookAt > 0.0f && t >= m_LookAt) { AutoTestLog("fireice look A_burn"); m_LookAt = 0.0f; }
        if (t < m_Next) return;
        m_Next += 1.0f;
        Read("A");
        if (t > 3.0f && Burning() == 0)
        {
            snprintf(line, sizeof(line), "fireice A stopped at %.1f s peak %u charred %u", t, m_Peak, m_Charred);
            AutoTestLog(line);
            AutoTestLog("fireice look A_end");
            m_T0 = m_AutoTime;
            m_AutoStep = 20;
        }
        else if (t > 90.0f)
        {
            AutoTestLog("fireice A DID NOT STOP in 90 s");
            m_T0 = m_AutoTime;
            m_AutoStep = 3;
        }
        break;
    case 20:
        if (t < 0.3f) return;
        m_GrassFire.Ignite(m_Patch[0], 1.5f);   // すぐ点け直す
        m_T0 = m_AutoTime;
        m_AutoStep = 3;
        break;
    case 3:
        if (t < 1.5f) return;
        snprintf(line, sizeof(line), "fireice A2 reignite right away: burning %u (expect 0)", Burning());
        AutoTestLog(line);
        m_AutoStep = 4;
        break;
    case 4:
        if (t < 40.0f) return;
        m_GrassFire.Ignite(m_Patch[0], 1.5f);
        m_T0 = m_AutoTime;
        m_AutoStep = 5;
        break;
    case 5:
        if (t < 2.0f) return;
        snprintf(line, sizeof(line), "fireice A3 reignite after 40 s: burning %u (expect > 0)", Burning());
        AutoTestLog(line);
        // ---- B: 帯の中の敵だけ焼ける ----
        {
            const float gy = m_Swarm.GetAIParams().groundY;
            const Vector3 c = m_Patch[1];
            for (int k = 0; k < 6; ++k)
            {
                const float a = 6.2831853f * (float)k / 6.0f;
                const float x = c.x + std::sin(a) * 2.5f, z = c.z + std::cos(a) * 2.5f;
                m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 1000.0f, 0.0f, Swarm::kEnemyKindMob);
            }
            int outside = 0;
            for (float r = 5.0f; r < 28.0f && outside < 4; r += 1.0f)
                for (int k = 0; k < 16 && outside < 4; ++k)
                {
                    const float a = 6.2831853f * (float)k / 16.0f;
                    const float x = c.x + std::sin(a) * r, z = c.z + std::cos(a) * r;
                    if (m_GrassFire.IsDry(x, z)) continue;
                    bool nearDry = false;
                    for (int q = 0; q < 8; ++q)
                    {
                        const float b = 0.7853982f * (float)q;
                        if (m_GrassFire.IsDry(x + std::sin(b) * 3.0f, z + std::cos(b) * 3.0f)) nearDry = true;
                    }
                    if (nearDry) continue;
                    m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 1000.0f, 0.0f, Swarm::kEnemyKindMob);
                    ++outside;
                }
            LookAt(c);
        }
        m_T0 = m_AutoTime;
        m_AutoStep = 6;
        break;
    case 6:
        if (t < 1.0f) return;
        m_GrassFire.Ignite(m_Patch[1], 1.5f);
        m_T0 = m_AutoTime;
        m_LookAt = 4.0f;
        m_AutoStep = 7;
        break;
    case 7:
        if (m_LookAt > 0.0f && t >= m_LookAt) { AutoTestLog("fireice look B_burn"); m_LookAt = 0.0f; }
        if (t < 12.0f) return;
        {
            std::vector<Swarm::Enemy> en; std::vector<uint32_t> st;
            float inLost = 0.0f, outLost = 0.0f; int inN = 0, outN = 0;
            if (m_Swarm.DebugReadEnemies(en, st))
                for (size_t i = 0; i < en.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    const float lost = 1000.0f - Swarm::HpFromFixed(en[i].hp);
                    if (m_GrassFire.IsDry(en[i].position.x, en[i].position.z)) { inLost += lost; ++inN; }
                    else { outLost += lost; ++outN; }
                }
            snprintf(line, sizeof(line), "fireice B on dry: n %d lost avg %.1f / on green: n %d lost avg %.1f",
                inN, inN ? inLost / inN : 0.0f, outN, outN ? outLost / outN : 0.0f);
            AutoTestLog(line);
        }
        m_Swarm.KillAll();
        m_GrassFire.Reset();   // B の火が残っていると C の数に混ざる
        LookAt(m_Patch[2]);
        m_GrassFire.Ignite(m_Patch[2], 1.5f);
        m_T0 = m_AutoTime;
        m_AutoStep = 8;
        break;

    // ---- C: 氷で消す ----
    case 8:
        if (t < 3.0f) return;
        Read("C before");
        AutoTestLog("fireice look C_burn");
        IceArea(m_Patch[2], 25.0f);
        m_T0 = m_AutoTime;
        m_AutoStep = 9;
        break;
    case 9:
        if (t < 0.6f) return;
        Read("C after");
        AutoTestLog("fireice look C_out");
        m_T0 = m_AutoTime;
        m_AutoStep = 10;
        break;
    case 10:
        if (t < 0.4f) return;
        if (!FindPond()) { AutoTestLog("fireice no pond"); AutoTestLog("fireice done"); m_AutoStep = 99; return; }
        LookAt(m_Pond);
        m_Camera.Camera().distance = 13.0f;
        m_Camera.Camera().SetPitch(42.0f);
        m_Camera.Camera().SnapToTarget();
        {
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            const float x = m_Pond.x + 8.0f, z = m_Pond.z;
            tf.position = Vector3(x, m_Grid.SampleHeight(x, z) + 1.0f, z);
        }
        m_T0 = m_AutoTime;
        m_AutoStep = 11;
        break;

    // ---- D: 池を凍らせる ----
    case 11:
        if (t < 1.0f) return;
        AutoTestLog("fireice look D_water");
        IceArea(m_Pond, 7.0f);
        m_T0 = m_AutoTime;
        m_AutoStep = 12;
        break;
    case 12:
        if (t < 0.4f) return;
        snprintf(line, sizeof(line), "fireice D frozen texels %.0f (pond %u cells)", m_Water.DebugIceCells(), m_Water.PondCells());
        AutoTestLog(line);
        AutoTestLog("fireice look D_ice");
        SpawnWalkers();
        m_SpeedSum = 0.0f; m_SpeedN = 0;
        m_Next = 0.0f;
        m_T0 = m_AutoTime;
        m_AutoStep = 13;
        break;
    case 13:
        if (t >= m_Next) { m_Next += 0.25f; Speeds(m_SpeedSum, m_SpeedN); }
        if (t < 4.5f) return;
        snprintf(line, sizeof(line), "fireice D on ice: samples %d avg speed %.2f", m_SpeedN, m_SpeedN ? m_SpeedSum / m_SpeedN : 0.0f);
        AutoTestLog(line);
        AutoTestLog("fireice look D_walk");
        m_Swarm.KillAll();
        m_T0 = m_AutoTime;
        m_AutoStep = 14;
        break;
    case 14:
        if (t < 2.2f) return;
        AutoTestLog("fireice look D_melt");
        m_T0 = m_AutoTime;
        m_AutoStep = 15;
        break;
    case 15:
        if (t < 2.5f) return;
        snprintf(line, sizeof(line), "fireice D melted: frozen texels %.0f (expect 0)", m_Water.DebugIceCells());
        AutoTestLog(line);
        SpawnWalkers();
        m_SpeedSum = 0.0f; m_SpeedN = 0;
        m_Next = 0.0f;
        m_T0 = m_AutoTime;
        m_AutoStep = 16;
        break;
    case 16:
        if (t >= m_Next) { m_Next += 0.25f; Speeds(m_SpeedSum, m_SpeedN); }
        if (t < 4.5f) return;
        snprintf(line, sizeof(line), "fireice D in water: samples %d avg speed %.2f", m_SpeedN, m_SpeedN ? m_SpeedSum / m_SpeedN : 0.0f);
        AutoTestLog(line);
        m_Swarm.KillAll();
        {
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            tf.position = Vector3(m_Pond.x, m_Pond.y + 0.2f, m_Pond.z);
        }
        m_Camera.SetTargetOverride(nullptr);
        m_Camera.Camera().distance = 7.5f;
        m_Camera.Camera().SetPitch(16.0f);
        m_Camera.Camera().SnapToTarget();
        m_T0 = m_AutoTime;
        m_AutoStep = 17;
        break;

    // ---- E: 氷の上に立つ ----
    case 17:
        if (t < 1.0f) return;
        LogFeet("E water");
        IceArea(m_Pond, 7.0f);
        m_T0 = m_AutoTime;
        m_AutoStep = 18;
        break;
    case 18:
        if (t < 0.4f) return;
        {
            const float gy = m_Swarm.GetAIParams().groundY;
            for (int k = 0; k < 4; ++k)
            {
                const float a = 1.5707963f * (float)k + 0.8f;
                const float x = m_Pond.x + std::sin(a) * 2.2f, z = m_Pond.z + std::cos(a) * 2.2f;
                m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
            }
        }
        m_T0 = m_AutoTime;
        m_AutoStep = 19;
        break;
    case 19:
        if (t < 0.6f) return;
        LogFeet("E ice");
        AutoTestLog("fireice look E_stand");
        m_PlayerControlSystem.testJump = true;
        m_T0 = m_AutoTime;
        m_AutoStep = 21;
        break;
    case 21:
        m_PlayerControlSystem.testJump = false;
        if (t < 0.35f) return;
        LogFeet("E jump");
        m_T0 = m_AutoTime;
        m_AutoStep = 22;
        break;
    case 22:
        if (t < 1.2f) return;
        LogFeet("E landed");
        m_PlayerControlSystem.testMove = Vector2(0.0f, 0.4f);
        m_T0 = m_AutoTime;
        m_AutoStep = 23;
        break;
    case 23:
        if (t < 0.5f) return;
        LogFeet("E run");
        AutoTestLog("fireice look E_run");
        m_PlayerControlSystem.testMove = Vector2::Zero;
        m_T0 = m_AutoTime;
        m_AutoStep = 24;
        break;
    case 24:
        if (t < 7.0f) return;
        LogFeet("E melted");
        AutoTestLog("fireice look E_melt");
        m_T0 = m_AutoTime;
        m_AutoStep = 25;
        break;
    case 25:
        if (t < 0.4f) return;
        AutoTestLog("fireice done");
        m_AutoStep = 99;
        break;
    default:
        break;
    }
}

void AutoTestFireGame::Run(float dt)
{
    char line[200];
    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Weather.eventsEnabled = false;
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ClearBackpackItems(bp);
        FillAllFrames(bp);
        const int lo = BackpackComponent::GRID / 2 - 1;
        BackpackLogic::Place(bp, ItemID::Meteor, lo, lo, 0);
        BackpackLogic::Place(bp, ItemID::Fireball, lo + 3, lo + 1, 0);
        bp.dirty = true;
        m_PlayerControlSystem.testInput = true;
        m_AutoStep = 1;
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_FIREGAME_SEC", env, sizeof(env)) > 0) m_End = (float)atof(env);
        m_Next = 10.0f;
    }
    if (m_AutoStep != 1) return;

    // 円を描いて走り続ける（火の中へ敵を連れて行く）
    const float a = m_AutoTime * 0.5f;
    m_PlayerControlSystem.testMove = Vector2(std::cos(a), std::sin(a));
    ++m_Frames;
    m_Span += dt;

    if (m_AutoTime < m_Next) return;
    m_Next += 10.0f;
    uint32_t b = 0, c = 0;
    m_GrassFire.DebugReadStats(b, c);
    m_PeakBurn = (std::max)(m_PeakBurn, b);
    const uint32_t kills = m_Swarm.GetCounters().killCount;
    snprintf(line, sizeof(line), "firegame t %.0f burning %u charred %u (%.1f%% of the map) kills +%u fps %.0f",
        m_AutoTime, b, c, 100.0f * (float)c / (float)(GrassFireField::kSize * GrassFireField::kSize),
        kills - m_LastKills, m_Span > 0.0f ? (float)m_Frames / m_Span : 0.0f);
    AutoTestLog(line);
    m_LastKills = kills;
    m_Frames = 0;
    m_Span = 0.0f;
    if (m_AutoTime >= 40.0f && m_AutoTime < 50.0f) AutoTestLog("firegame look mid");
    if (m_AutoTime >= m_End)
    {
        snprintf(line, sizeof(line), "firegame peak burning %u", m_PeakBurn);
        AutoTestLog(line);
        AutoTestLog("firegame done");
        m_AutoStep = 2;
    }
}

REGISTER_BATTLE_AUTOTEST("fireice", AutoTestFireIce)
REGISTER_BATTLE_AUTOTEST("firegame", AutoTestFireGame)
