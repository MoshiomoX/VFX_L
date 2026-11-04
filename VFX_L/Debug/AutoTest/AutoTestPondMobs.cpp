// ============================================================
// AutoTestPondMobs.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=pondmobs（VFXL_STAGE=2 で砂漠のオアシス）
// 水たまりに雑魚が入れるか・水が弾を止めないか（2026-10-28。それまで水たまりのマスは塞いでいた）
//   1 秒: 湧き停止・全消し・無敵・詠唱停止。一番近い「周り 8 マスも水」の水たまりのマスを探す
//   A 雑魚が水へ : プレイヤーを水の真ん中に立たせ、周りの乾いた地面 9〜14m に雑魚 12 体（HP 10 万）。10 秒、
//                  0.5 秒毎に `pondmobs A t alive inWater near far stuck ripple up moving`（near = 2.5m 以内、stuck = 3m より遠くで
//                  2 秒動かない）、2.2 / 6 秒 `pondmobs look a1 / a2`（ripple = 波紋の一番大きな |高さ|、雑魚が水の中を歩くと立つ。2026-10-28）、最後 `pondmobs A result reached inWaterMax stuckMax`
//   B 水越しの弾 : 全消し。プレイヤーは水の 6m 手前の乾いた地面、的 3 体（動かない、HP 10 万）は水の中。
//                  初期の魔法（ファイアボール）で 5 秒撃つ → `pondmobs B lost <HP> hit <体数>`、2 秒 `pondmobs look b`
//   `pondmobs done`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPondMobs final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Place(float x, float z);
        void Face(const Vector3& from, const Vector3& to, float distance, float pitch);
        bool FindPond();
        void SpawnRing();
        void SpawnTargets();
        bool CellIsWater(float x, float z) const;
        void MeasurePonds();   // 全部の水たまりの水面の傾きと、岸の地面より水面が高いか（膨らみの上の水たまり）

        Vector3 m_Pond;          // 水たまりの真ん中のマスの中心（水面の高さ）
        Vector3 m_Stand;         // B 段で立つ乾いた地面
        float m_PhaseAt = 0.0f;
        float m_NextLog = 0.0f;
        float m_NextSnap = 0.0f;
        int m_Reached = 0, m_InWaterMax = 0, m_StuckMax = 0;
        bool m_LookA1 = false, m_LookA2 = false, m_LookB = false;
        std::vector<Vector3> m_Snap;   // 2 秒前の位置（槽毎、stuck の判定）
    };
}

void AutoTestPondMobs::Place(float x, float z)
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    tf.position = Vector3(x, m_Grid.SampleHeight(x, z) + 1.0f, z);
    if (m_Registry.Has<RigidbodyComponent>(m_Player))
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
}

// from から to の方を見る（FollowCamera: forward = (-sin yaw, 0, cos yaw)）
void AutoTestPondMobs::Face(const Vector3& from, const Vector3& to, float distance, float pitch)
{
    auto& cam = m_Camera.Camera();
    const Vector3 d = to - from;
    cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-d.x, d.z)));
    cam.SetPitch(pitch);
    cam.distance = distance;
    cam.avoidOcclusion = false;
    cam.SnapToTarget();
}

bool AutoTestPondMobs::CellIsWater(float x, float z) const
{
    int gx = 0, gz = 0;
    m_Grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
    return m_Grid.IsWater(gx, gz);
}

// 周り 5×5 マスに水が一番多い水たまりのマス（同じなら近い方）。周り 8 マスは全部水のこと（AutoTestWater と同じ）
bool AutoTestPondMobs::FindPond()
{
    const float kCs = GridWorld::kCellSize;
    const Vector3 p = m_Registry.Get<TransformComponent>(m_Player).position;
    float best = 1.0e9f;
    bool found = false;
    for (int gz = 1; gz < m_Grid.Depth() - 1; ++gz)
        for (int gx = 1; gx < m_Grid.Width() - 1; ++gx)
        {
            if (!m_Grid.IsWater(gx, gz)) continue;
            int around = 0;
            bool inner = true;
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                {
                    const bool w = m_Grid.IsWater(gx + dx, gz + dz);
                    around += w ? 1 : 0;
                    if (!w && std::abs(dx) <= 1 && std::abs(dz) <= 1) inner = false;
                }
            if (!inner) continue;
            const float x = m_Grid.OriginX() + (gx + 0.5f) * kCs, z = m_Grid.OriginZ() + (gz + 0.5f) * kCs;
            const float d = (Vector3(x, 0.0f, z) - Vector3(p.x, 0.0f, p.z)).Length();
            const float score = -10.0f * around + 0.05f * d;
            if (score < best)
            {
                float wy = 0.0f;
                if (!m_Water.WaterAt(x, z, wy)) wy = m_Grid.SampleHeight(x, z);
                best = score; m_Pond = Vector3(x, wy, z); found = true;
            }
        }
    return found;
}

// 水たまり（4 近傍で繋がったマス）毎に：tilt = マスの真ん中の水面の高さの最高 − 最低（大きい = 坂の上で水面が傾いている）、
// above = 水面の平均 − 岸（隣の乾いたマス）の地面の平均（> 0 = 水面が周りの地面より高い = 膨らみの上）
// → `pondmobs ponds n cells c tiltMax tiltMean tilted(>0.3m) aboveMax raised(>0.05m)`（2026-10-28）
void AutoTestPondMobs::MeasurePonds()
{
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    std::vector<int> comp((size_t)W * D, -1);
    int ponds = 0, cells = 0, tilted = 0, raised = 0;
    float tiltMax = 0.0f, tiltSum = 0.0f, aboveMax = -1.0e9f;
    std::vector<int> stack;
    for (int z0 = 0; z0 < D; ++z0)
        for (int x0 = 0; x0 < W; ++x0)
        {
            if (!m_Grid.IsWater(x0, z0) || comp[(size_t)z0 * W + x0] >= 0) continue;
            float lo = 1.0e9f, hi = -1.0e9f, surfSum = 0.0f, bankSum = 0.0f;
            int n = 0, bank = 0;
            stack.assign(1, z0 * W + x0);
            comp[(size_t)z0 * W + x0] = ponds;
            while (!stack.empty())
            {
                const int c = stack.back();
                stack.pop_back();
                const int x = c % W, z = c / W;
                const Vector3 w = m_Grid.CellToWorld(x, z);
                float sy = 0.0f;
                if (!m_Water.WaterAt(w.x, w.z, sy)) sy = m_Grid.SampleHeight(w.x, w.z);
                lo = (std::min)(lo, sy); hi = (std::max)(hi, sy); surfSum += sy; ++n;
                static const int kD[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (const auto& d : kD)
                {
                    const int nx = x + d[0], nz = z + d[1];
                    if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                    if (m_Grid.IsWater(nx, nz))
                    {
                        if (comp[(size_t)nz * W + nx] < 0) { comp[(size_t)nz * W + nx] = ponds; stack.push_back(nz * W + nx); }
                    }
                    else if (m_Grid.IsWalkable(nx, nz))
                    {
                        const Vector3 b = m_Grid.CellToWorld(nx, nz);
                        bankSum += m_Grid.SampleHeight(b.x, b.z); ++bank;
                    }
                }
            }
            const float tilt = hi - lo;
            const float above = bank > 0 ? surfSum / n - bankSum / bank : 0.0f;
            ++ponds; cells += n; tiltSum += tilt;
            tiltMax = (std::max)(tiltMax, tilt);
            aboveMax = (std::max)(aboveMax, above);
            if (tilt > 0.3f) ++tilted;
            if (above > 0.05f) ++raised;
        }
    char line[200];
    snprintf(line, sizeof(line), "pondmobs ponds %d cells %d tiltMax %.2f tiltMean %.2f tilted %d aboveMax %.2f raised %d",
        ponds, cells, tiltMax, ponds ? tiltSum / ponds : 0.0f, tilted, ponds ? aboveMax : 0.0f, raised);
    AutoTestLog(line);
}

// 水の真ん中のプレイヤーの周り、乾いた歩ける地面 9〜14m に雑魚 12 体
void AutoTestPondMobs::SpawnRing()
{
    const float gy = m_Swarm.GetAIParams().groundY;
    int placed = 0;
    for (int k = 0; k < 64 && placed < 12; ++k)
    {
        const float a = k * 2.3999632f;   // 黄金角
        const float r = 9.0f + 5.0f * std::fmod(k * 0.618034f, 1.0f);
        const float x = m_Pond.x + std::cos(a) * r, z = m_Pond.z + std::sin(a) * r;
        int gx = 0, gz = 0;
        m_Grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
        if (!m_Grid.IsDryWalkable(gx, gz)) continue;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 100000.0f, 3.5f);
        ++placed;
    }
    char line[96];
    snprintf(line, sizeof(line), "pondmobs A spawned %d", placed);
    AutoTestLog(line);
}

// 水の中に動かない的 3 体
void AutoTestPondMobs::SpawnTargets()
{
    const float gy = m_Swarm.GetAIParams().groundY;
    static const float kOff[3][2] = { { 0.0f, 0.0f }, { 1.2f, 0.6f }, { -1.0f, -0.8f } };
    for (const auto& o : kOff)
    {
        const float x = m_Pond.x + o[0], z = m_Pond.z + o[1];
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 100000.0f, 0.0f);
    }
}

void AutoTestPondMobs::Run()
{
    auto& pcs = m_PlayerControlSystem;
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
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
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        m_Weather.eventsEnabled = false;
        m_Weather.timeOverride = 0.0f;

        int waterCells = 0;
        for (int gz = 0; gz < m_Grid.Depth(); ++gz)
            for (int gx = 0; gx < m_Grid.Width(); ++gx)
                waterCells += m_Grid.IsWater(gx, gz) ? 1 : 0;
        const bool found = FindPond();
        snprintf(line, sizeof(line), "pondmobs water cells %d found %d at (%.1f, %.1f) y %.2f ground %.2f",
            waterCells, found ? 1 : 0, m_Pond.x, m_Pond.z, m_Pond.y, m_Grid.SampleHeight(m_Pond.x, m_Pond.z));
        AutoTestLog(line);
        MeasurePonds();
        if (!found) { AutoTestLog("pondmobs no pond"); AutoTestLog("pondmobs done"); m_AutoStep = 99; return; }

        if (GetEnvironmentVariableA("VFXL_NO_MOBWAKE", nullptr, 0) > 0) m_Water.GetSettings().mobWake = false;   // 比べる用
        Place(m_Pond.x, m_Pond.z);
        Face(Vector3(m_Pond.x, 0.0f, m_Pond.z - 1.0f), m_Pond, 13.0f, 42.0f);
        m_GameUI.hideAll = true;
        SpawnRing();
        m_PhaseAt = m_NextLog = m_NextSnap = m_AutoTime;
        m_AutoStep = 1;
        return;
    }

    const float t = m_AutoTime - m_PhaseAt;
    if (m_AutoStep == 1)   // A 雑魚が水へ
    {
        pcs.testMove = Vector2::Zero;
        if (m_AutoTime >= m_NextLog)
        {
            m_NextLog = m_AutoTime + 0.5f;
            std::vector<Swarm::Enemy> en;
            std::vector<uint32_t> st;
            if (m_Swarm.DebugReadEnemies(en, st))
            {
                const bool snap = m_AutoTime >= m_NextSnap;
                if (m_Snap.size() != en.size()) m_Snap.assign(en.size(), Vector3(1.0e9f, 0.0f, 0.0f));
                int alive = 0, inWater = 0, nearN = 0, stuck = 0;
                float farD = 0.0f;
                for (size_t i = 0; i < en.size() && i < st.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    const Vector3 e(en[i].position.x, en[i].position.y, en[i].position.z);
                    ++alive;
                    if (CellIsWater(e.x, e.z)) ++inWater;
                    const float d = Vector2(e.x - tf.position.x, e.z - tf.position.z).Length();
                    if (d < 2.5f) ++nearN;
                    farD = (std::max)(farD, d);
                    if (snap)
                    {
                        if (d > 3.0f && t > 2.5f && Vector2(e.x - m_Snap[i].x, e.z - m_Snap[i].z).Length() < 0.3f) ++stuck;
                        m_Snap[i] = e;
                    }
                }
                if (snap) m_NextSnap = m_AutoTime + 2.0f;
                m_Reached = (std::max)(m_Reached, nearN);
                m_InWaterMax = (std::max)(m_InWaterMax, inWater);
                m_StuckMax = (std::max)(m_StuckMax, stuck);
                float maxAbs = 0.0f, up = 0.0f;
                uint32_t moving = 0;
                m_Water.DebugReadRipples(maxAbs, moving, &up);   // 雑魚の波紋（2026-10-28）
                snprintf(line, sizeof(line), "pondmobs A t %.1f alive %d inWater %d near %d far %.1f stuck %d ripple %.4f up %.4f moving %u",
                    t, alive, inWater, nearN, farD, stuck, maxAbs, up, moving);
                AutoTestLog(line);
            }
        }
        if (!m_LookA1 && t >= 2.2f) { m_LookA1 = true; AutoTestLog("pondmobs look a1"); }
        if (!m_LookA2 && t >= 6.0f) { m_LookA2 = true; AutoTestLog("pondmobs look a2"); }
        if (t >= 10.0f)
        {
            snprintf(line, sizeof(line), "pondmobs A result reached %d inWaterMax %d stuckMax %d", m_Reached, m_InWaterMax, m_StuckMax);
            AutoTestLog(line);
            // B の準備：水の 6m 手前の乾いた地面（16 方向で水面と高さが近い所）
            m_Swarm.KillAll();
            float bestScore = 1.0e9f;
            m_Stand = m_Pond;
            for (int k = 0; k < 16; ++k)
            {
                const float a = k * 6.2831853f / 16.0f;
                for (float r = 5.0f; r <= 8.0f; r += 1.0f)
                {
                    const float x = m_Pond.x + std::cos(a) * r, z = m_Pond.z + std::sin(a) * r;
                    int gx = 0, gz = 0;
                    m_Grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
                    if (!m_Grid.IsDryWalkable(gx, gz)) continue;
                    const float score = std::abs(m_Grid.SampleHeight(x, z) - m_Pond.y) + 0.1f * r;
                    if (score < bestScore) { bestScore = score; m_Stand = Vector3(x, 0.0f, z); }
                    break;
                }
            }
            Place(m_Stand.x, m_Stand.z);
            Face(m_Stand, m_Pond, 12.0f, 35.0f);
            m_AutoStep = 2;
            m_PhaseAt = m_AutoTime;
        }
        return;
    }
    if (m_AutoStep == 2 && t >= 0.5f)   // B 水越しの弾
    {
        SpawnTargets();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        snprintf(line, sizeof(line), "pondmobs B stand (%.1f, %.1f) ground %.2f -> pond %.1f m", m_Stand.x, m_Stand.z,
            m_Grid.SampleHeight(m_Stand.x, m_Stand.z), Vector2(m_Pond.x - m_Stand.x, m_Pond.z - m_Stand.z).Length());
        AutoTestLog(line);
        m_AutoStep = 3;
        m_PhaseAt = m_AutoTime;
        return;
    }
    if (m_AutoStep == 3)
    {
        if (!m_LookB && t >= 2.0f) { m_LookB = true; AutoTestLog("pondmobs look b"); }
        if (t >= 5.0f)
        {
            std::vector<Swarm::Enemy> en;
            std::vector<uint32_t> st;
            float lost = 0.0f;
            int hit = 0;
            if (m_Swarm.DebugReadEnemies(en, st))
                for (size_t i = 0; i < en.size() && i < st.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    const float hp = Swarm::HpFromFixed(en[i].hp);
                    if (hp < 100000.0f - 0.5f) { ++hit; lost += 100000.0f - hp; }
                }
            snprintf(line, sizeof(line), "pondmobs B lost %.0f hit %d", lost, hit);
            AutoTestLog(line);
            AutoTestLog("pondmobs done");
            m_AutoStep = 99;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("pondmobs", AutoTestPondMobs)
