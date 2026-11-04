// ============================================================
// AutoTestClimb.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=climb（2026-10-29、雑魚が壁を登る。ユーザー「でないと無敵の場所ができる」）
//   A 岩  : 場内の登れる障害物（GridWorld の雑魚用の高さ − 地面 が 1.2〜4m の細かいマス）をプレイヤーの近くで探し、その上に
//           プレイヤーを立たせる。周り 6〜10m に雑魚 16 体（HP 10 万、普段の速さ）。以前はここが無敵の場所（届かない）だった
//   B 崖  : 段々の台地の崖（歩けるマスの隣が 2m 以上高い歩けるマス）を探し、プレイヤーを台地の上（崖から 1 マス内側）へ。
//           崖の下の 4〜10m に雑魚 16 体
//   どちらも 0.5 秒毎に `climb <A|B> t alive near up climbing lost`（near = プレイヤーから水平 1.6m・高さ 1.5m 以内、
//   up = 足元の地面より 1m 以上高い所に立っている、climbing = 足元より 0.5m 以上浮いていて落ちていない（壁の途中）、
//   lost = プレイヤーの減った HP）。1.5 / 4 秒に `climb look <A|B><n>`、12 秒で `climb <A|B> result firstNear <秒> lost <HP>`
// 期待：A で lost > 0（雑魚が登って殴る）、B で崖を登る雑魚が居て、坂を回るより早く着く
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestClimb final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        bool SetupRock();
        bool SetupCliff();
        void SpawnRing(const Vector3& centre, float rMin, float rMax, float maxBelowDiff);
        void Sample(const char* phase);

        float m_PhaseAt = 0.0f, m_NextLog = 0.0f, m_FirstNear = -1.0f, m_Hp0 = 0.0f;
        int   m_Looks = 0;
        Vector3 m_Stand;
        float m_StandFeet = 0.0f;
    };

    constexpr float kPhaseTime = 12.0f;
    constexpr float kHp = 100000.0f;
}

// 周りの rMin〜rMax の歩けるマス（足元がプレイヤーより maxBelowDiff 以上低い所だけ = 崖の下）に雑魚 16 体
void AutoTestClimb::SpawnRing(const Vector3& centre, float rMin, float rMax, float maxBelowDiff)
{
    const float gy = m_Swarm.GetAIParams().groundY;
    int placed = 0;
    for (int k = 0; k < 64 && placed < 16; ++k)
    {
        const float a = 6.2831853f * (float)k / 16.0f + 0.37f * (float)(k / 16);
        const float r = rMin + (rMax - rMin) * (float)((k * 7) % 16) / 15.0f;
        const float x = centre.x + std::cos(a) * r, z = centre.z + std::sin(a) * r;
        int gx = 0, gz = 0;
        m_Grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
        if (!m_Grid.IsWalkable(gx, gz)) continue;
        const float g = m_Grid.SampleHeight(x, z);
        if (g > m_StandFeet - maxBelowDiff) continue;
        m_Swarm.SpawnEnemy(Vector3(x, g + gy, z), kHp, 3.5f);
        ++placed;
    }
    char line[96];
    snprintf(line, sizeof(line), "climb spawned %d", placed);
    AutoTestLog(line);
}

bool AutoTestClimb::SetupRock()
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const int HW = m_Grid.HeightW(), HD = m_Grid.HeightD();
    float bestD = 1e30f;
    int bx = -1, bz = -1;
    for (int hz = 0; hz < HD; ++hz)
        for (int hx = 0; hx < HW; ++hx)
        {
            const float rise = m_Grid.MobHeightAt(hx, hz) - m_Grid.HeightAt(hx, hz);
            if (rise < 1.2f || rise > 4.0f) continue;
            // 上面の真ん中の方（周りの細かいマスも同じ高さ）
            bool flat = true;
            for (int dz = -1; dz <= 1 && flat; ++dz)
                for (int dx = -1; dx <= 1 && flat; ++dx)
                    flat = std::fabs(m_Grid.MobHeightAt(hx + dx, hz + dz) - m_Grid.MobHeightAt(hx, hz)) < 0.05f;
            if (!flat) continue;
            const Vector3 c = m_Grid.HeightCellToWorld(hx, hz);
            const float d = (c.x - tf.position.x) * (c.x - tf.position.x) + (c.z - tf.position.z) * (c.z - tf.position.z);
            if (d < bestD) { bestD = d; bx = hx; bz = hz; }
        }
    char line[200];
    if (bx < 0) { AutoTestLog("climb A: no climbable rock found"); return false; }
    const Vector3 c = m_Grid.HeightCellToWorld(bx, bz);
    m_StandFeet = m_Grid.MobHeightAt(bx, bz);
    m_Stand = Vector3(c.x, m_StandFeet + 1.0f, c.z);
    tf.position = m_Stand;
    if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
    snprintf(line, sizeof(line), "climb A rock at (%.1f, %.1f) top %.2f ground %.2f dist %.1f", c.x, c.z, m_StandFeet,
        m_Grid.HeightAt(bx, bz), std::sqrt(bestD));
    AutoTestLog(line);
    SpawnRing(c, 6.0f, 10.0f, 1.0f);
    return true;
}

bool AutoTestClimb::SetupCliff()
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    float bestD = 1e30f;
    int bx = -1, bz = -1, dirX = 0, dirZ = 0;
    const int dx4[4] = { 1, -1, 0, 0 }, dz4[4] = { 0, 0, 1, -1 };
    for (int z = 2; z < D - 2; ++z)
        for (int x = 2; x < W - 2; ++x)
        {
            if (!m_Grid.IsWalkable(x, z)) continue;
            const Vector3 c = m_Grid.CellToWorld(x, z);
            const float h = m_Grid.SampleHeight(c.x, c.z);
            for (int k = 0; k < 4; ++k)
            {
                // x,z = 台地の上のマス、その外（崖の下）が 2m 以上低い。さらに内側のマスも台地
                const int ox = x + dx4[k], oz = z + dz4[k], ix = x - dx4[k], iz = z - dz4[k];
                if (!m_Grid.IsWalkable(ox, oz) || !m_Grid.IsWalkable(ix, iz)) continue;
                const Vector3 co = m_Grid.CellToWorld(ox, oz), ci = m_Grid.CellToWorld(ix, iz);
                if (h - m_Grid.SampleHeight(co.x, co.z) < 2.0f || std::fabs(m_Grid.SampleHeight(ci.x, ci.z) - h) > 0.5f) continue;
                const float d = (c.x - tf.position.x) * (c.x - tf.position.x) + (c.z - tf.position.z) * (c.z - tf.position.z);
                if (d < bestD) { bestD = d; bx = x; bz = z; dirX = dx4[k]; dirZ = dz4[k]; }
            }
        }
    char line[200];
    if (bx < 0) { AutoTestLog("climb B: no cliff found"); return false; }
    const Vector3 c = m_Grid.CellToWorld(bx - dirX, bz - dirZ);   // 崖から 1 マス内側
    m_StandFeet = m_Grid.SampleHeight(c.x, c.z);
    m_Stand = Vector3(c.x, m_StandFeet + 1.0f, c.z);
    tf.position = m_Stand;
    if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
    const Vector3 edge = m_Grid.CellToWorld(bx, bz);
    snprintf(line, sizeof(line), "climb B cliff at (%.1f, %.1f) top %.2f outward (%d, %d) dist %.1f", edge.x, edge.z, m_StandFeet,
        dirX, dirZ, std::sqrt(bestD));
    AutoTestLog(line);
    // 崖の下：外向きに 4〜10m の所を中心に
    const Vector3 below = Vector3(edge.x + dirX * 5.0f, 0.0f, edge.z + dirZ * 5.0f);
    SpawnRing(below, 0.5f, 5.0f, 1.5f);
    // カメラは崖を横から
    auto& cam = m_Camera.Camera();
    cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2((float)-dirZ, (float)dirX)));
    return true;
}

void AutoTestClimb::Sample(const char* phase)
{
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    const float t = m_AutoTime - m_PhaseAt;
    if (m_AutoTime < m_NextLog) return;
    m_NextLog = m_AutoTime + 0.5f;
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    if (!m_Swarm.DebugReadEnemies(en, st)) return;
    const float gy = m_Swarm.GetAIParams().groundY;
    int alive = 0, nearN = 0, up = 0, climbing = 0;
    for (size_t i = 0; i < en.size(); ++i)
    {
        if (st[i] == Swarm::kStateDead) continue;
        ++alive;
        const Vector3 p = en[i].position;
        const float feet = p.y - gy;
        const float ground = m_Grid.SampleHeight(p.x, p.z);
        const float dx = p.x - tf.position.x, dz = p.z - tf.position.z;
        if (dx * dx + dz * dz < 1.6f * 1.6f && std::fabs(feet - (tf.position.y - 1.0f)) < 1.5f) ++nearN;
        if (feet > ground + 1.0f) ++up;
        if (feet > ground + 0.5f && en[i].velocity.y > -0.5f) ++climbing;
    }
    if (nearN > 0 && m_FirstNear < 0.0f) m_FirstNear = t;
    char line[200];
    snprintf(line, sizeof(line), "climb %s t %.1f alive %d near %d up %d climbing %d lost %.0f", phase, t, alive, nearN, up, climbing,
        m_Hp0 - hp.current);
    AutoTestLog(line);
}

void AutoTestClimb::Run()
{
    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    char line[200];

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        hp.max = hp.current = kHp;
        auto& cam = m_Camera.Camera();
        cam.SetPitch(30.0f);
        cam.distance = 14.0f;
        cam.avoidOcclusion = false;
        snprintf(line, sizeof(line), "climb start climbSpeed %.2f climbCost %.2f", m_Swarm.GetBomberParams().climbSpeed,
            m_Swarm.GetFlowField().climbCostPerMeter);
        AutoTestLog(line);
        if (!SetupRock()) { m_AutoStep = 3; return; }
        cam.SnapToTarget();
        m_Hp0 = hp.current;
        m_PhaseAt = m_AutoTime;
        m_NextLog = 0.0f;
        m_FirstNear = -1.0f;
        m_Looks = 0;
        m_AutoStep = 1;
        return;
    }
    auto keepStanding = [&]()
        {
            // 無敵にはしない（当たった数を数える）。HP が減り切らないように戻す。立つ所から押し出されたら戻す
            if (hp.current < kHp * 0.5f) { m_Hp0 += kHp * 0.5f - hp.current; hp.current = kHp * 0.5f; }
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            const float dx = tf.position.x - m_Stand.x, dz = tf.position.z - m_Stand.z;
            if (dx * dx + dz * dz > 1.0f) tf.position = m_Stand;
        };
    auto phase = [&](const char* name, int nextStep)
        {
            keepStanding();
            Sample(name);
            const float t = m_AutoTime - m_PhaseAt;
            if (m_Looks == 0 && t >= 1.5f) { snprintf(line, sizeof(line), "climb look %s0", name); AutoTestLog(line); ++m_Looks; }
            if (m_Looks == 1 && t >= 4.0f) { snprintf(line, sizeof(line), "climb look %s1", name); AutoTestLog(line); ++m_Looks; }
            if (t >= kPhaseTime)
            {
                snprintf(line, sizeof(line), "climb %s result firstNear %.1f lost %.0f", name, m_FirstNear, m_Hp0 - hp.current);
                AutoTestLog(line);
                m_AutoStep = nextStep;
            }
        };
    if (m_AutoStep == 1)
    {
        phase("A", 2);
        if (m_AutoStep == 2)
        {
            m_Swarm.KillAll();
            hp.current = kHp;
            if (!SetupCliff()) { m_AutoStep = 3; return; }
            m_Camera.Camera().SnapToTarget();
            m_Hp0 = hp.current;
            m_PhaseAt = m_AutoTime;
            m_NextLog = 0.0f;
            m_FirstNear = -1.0f;
            m_Looks = 0;
        }
        return;
    }
    if (m_AutoStep == 2)
    {
        phase("B", 3);
        return;
    }
    if (m_AutoStep == 3)
    {
        m_Swarm.KillAll();
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("climb done");
        m_AutoStep = 4;
    }
}

REGISTER_BATTLE_AUTOTEST("climb", AutoTestClimb)
