// ============================================================
// AutoTestDrop.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=drop
// VFXL_BATTLE_AUTOTEST=drop：台地の上の雑魚が崖下のプレイヤーへ縁から飛び降りるか（A）、プレイヤーも上に居る時は落ちないか（B）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestDrop final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 台地からの飛び降り（VFXL_BATTLE_AUTOTEST=drop、2026-10-01）
// 崖（地面のマスの隣が 2m 以上高い歩けるマス）をプレイヤーの近くで探し、その台地（高さ 2m 超で繋がった歩けるマス）を塗る。
// A（1 秒）: プレイヤーを崖下の地面に立たせ、台地の上の 3〜12m に雑魚 20 体。飛び降りて来るはず。
// B（11 秒）: プレイヤーを台地の上（崖から 2 マス内側）へ、雑魚 20 体も台地の上。プレイヤーが同じ高さなので誰も落ちないはず。
// 0.5 秒毎に `drop <A|B> t alive high falling near` （high = 足元 2m 超、falling = 足元より 0.3m 以上浮いている、
// near = プレイヤーから 3m 以内）。A の 2.5 / 4 秒、B の 4 秒に `drop look <A|B> <n>`。21 秒 done
// ============================================================
void AutoTestDrop::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;

    static int s_Gx = -1, s_Gz = -1, s_Px = -1, s_Pz = -1, s_Looks = 0;   // 崖下の地面 / 台地側のマス
    static std::vector<std::pair<int, int>> s_Top;                       // 台地の上のマス
    static float s_Start = 0.0f, s_NextLog = 0.0f, s_NextClip = 0.0f, s_ClipMax = 0.0f;
    static int s_ClipHits = 0;
    static char s_Phase = 'A';
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    auto cellH = [&](int gx, int gz) { const Vector3 c = m_Grid.CellToWorld(gx, gz); return m_Grid.SampleHeight(c.x, c.z); };
    auto walk = [&](int gx, int gz) { return gx >= 0 && gz >= 0 && gx < W && gz < D && m_Grid.IsWalkable(gx, gz); };
    const float gy = m_Swarm.GetAIParams().groundY;

    auto place = [&](int gx, int gz)
        {
            const Vector3 c = m_Grid.CellToWorld(gx, gz);
            tf.position = Vector3(c.x, m_Grid.SampleHeight(c.x, c.z) + 1.0f, c.z);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        };
    auto spawnOnTop = [&](int count)
        {
            const Vector3 pp = tf.position;
            int n = 0;
            for (size_t k = 0; k < s_Top.size() * 4 && n < count; ++k)
            {
                const auto& c = s_Top[(k * 7919) % s_Top.size()];
                const Vector3 w = m_Grid.CellToWorld(c.first, c.second);
                const float d = (Vector3(w.x, 0, w.z) - Vector3(pp.x, 0, pp.z)).Length();
                if (d < 3.0f || d > 12.0f) continue;
                const float j = (float)(k % 5) * 0.3f - 0.6f;
                m_Swarm.SpawnEnemy(Vector3(w.x + j, gy, w.z - j), 100000.0f, 3.5f);
                ++n;
            }
            return n;
        };
    // 横から：前方 = 崖の線に沿う向き（地面 → 台地 を 90 度回す）
    auto sideCamera = [&](float pitch, float dist)
        {
            const Vector3 g = m_Grid.CellToWorld(s_Gx, s_Gz), t = m_Grid.CellToWorld(s_Px, s_Pz);
            Vector3 d(t.x - g.x, 0.0f, t.z - g.z);
            d.Normalize();
            const float fx = -d.z, fz = d.x;
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-fx, fz)));
            cam.SetPitch(pitch);
            cam.distance = dist;
            cam.avoidOcclusion = false;
            cam.SnapToTarget();
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        int pgx, pgz;
        m_Grid.WorldToCell(tf.position, pgx, pgz);
        float best = 1e30f;
        for (int z = 3; z < D - 3; ++z)
            for (int x = 3; x < W - 3; ++x)
            {
                if (!walk(x, z) || cellH(x, z) > 0.3f) continue;
                const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (auto& dd : nb)
                {
                    const int nx = x + dd[0], nz = z + dd[1];
                    if (!walk(nx, nz) || cellH(nx, nz) < 2.0f) continue;
                    if (!walk(x - dd[0], z - dd[1]) || cellH(x - dd[0], z - dd[1]) > 0.3f) continue;
                    const float d2 = (float)((x - pgx) * (x - pgx) + (z - pgz) * (z - pgz));
                    if (d2 < best) { best = d2; s_Gx = x; s_Gz = z; s_Px = nx; s_Pz = nz; }
                }
            }
        if (s_Gx < 0) { AutoTestLog("drop no cliff found"); m_AutoStep = 9; return; }
        // 台地を塗る（高さ 2m 超で 4 近傍に繋がった歩けるマス）
        s_Top.clear();
        std::vector<uint8_t> seen((size_t)W * D, 0);
        std::vector<std::pair<int, int>> st{ { s_Px, s_Pz } };
        seen[(size_t)s_Pz * W + s_Px] = 1;
        while (!st.empty())
        {
            auto c = st.back(); st.pop_back();
            s_Top.push_back(c);
            const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (auto& dd : nb)
            {
                const int nx = c.first + dd[0], nz = c.second + dd[1];
                if (!walk(nx, nz) || seen[(size_t)nz * W + nx] || cellH(nx, nz) < 2.0f) continue;
                seen[(size_t)nz * W + nx] = 1;
                st.push_back({ nx, nz });
            }
        }
        place(s_Gx, s_Gz);
        const int n = spawnOnTop(20);
        sideCamera(25.0f, 14.0f);
        char line[160];
        snprintf(line, sizeof(line), "drop A ground (%d,%d) top (%d,%d) h %.2f plateau cells %d spawned %d",
            s_Gx, s_Gz, s_Px, s_Pz, cellH(s_Px, s_Pz), (int)s_Top.size(), n);
        AutoTestLog(line);
        s_Phase = 'A';
        s_Start = m_AutoTime;
        s_NextLog = m_AutoTime + 0.5f;
        s_Looks = 0;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime - s_Start >= 10.0f)
    {
        m_Swarm.KillAll();
        // 台地の上、崖から 2 マス内側（崖の向きの反対へ）
        const int dx = s_Px - s_Gx, dz = s_Pz - s_Gz;
        int tx = s_Px + dx * 2, tz = s_Pz + dz * 2;
        if (!walk(tx, tz) || cellH(tx, tz) < 2.0f) { tx = s_Px; tz = s_Pz; }
        place(tx, tz);
        const int n = spawnOnTop(20);
        sideCamera(35.0f, 16.0f);
        char line[96];
        snprintf(line, sizeof(line), "drop B player on top (%d,%d) spawned %d", tx, tz, n);
        AutoTestLog(line);
        s_Phase = 'B';
        s_Start = m_AutoTime;
        s_NextLog = m_AutoTime + 0.5f;
        s_Looks = 0;
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime - s_Start >= 10.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("drop done");
        m_AutoStep = 3;
    }

    // 貫通: 0.1 秒毎。体（見た目の幅 = 衝突半径 × 1.15）の中に、足より 0.3m 以上高い地形（崖の面・台地の縁。
    // 坂の段 0.27m は数えない）が入っている深さ。落ちる途中に崖の面が体を切る / 縁で台地に沈む = 画面上の貫通
    if ((m_AutoStep == 1 || m_AutoStep == 2) && m_AutoTime >= s_NextClip)
    {
        s_NextClip = m_AutoTime + 0.1f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        const float hs = GridWorld::kCellSize / (float)GridWorld::kHeightSub;
        const float vr = m_Swarm.GetAIParams().enemyRadius * 1.15f;
        auto rawAt = [&](float x, float z)
            {
                return m_Grid.HeightAt((int)std::floor((x - m_Grid.OriginX()) / hs), (int)std::floor((z - m_Grid.OriginZ()) / hs));
            };
        if (m_Swarm.DebugReadEnemies(enemies, states))
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                const Vector3 p = enemies[i].position;
                const float feet = p.y - gy;
                float pen = 0.0f;
                for (int k = 0; k < 16; ++k)
                {
                    const float a = (float)k * DirectX::XM_2PI / 16.0f;
                    for (float s = 0.05f; s <= vr + 1e-4f; s += 0.05f)
                        if (rawAt(p.x + std::cos(a) * s, p.z + std::sin(a) * s) > feet + 0.3f)
                        {
                            pen = (std::max)(pen, vr - s);
                            break;
                        }
                }
                if (pen > 0.05f) ++s_ClipHits;
                s_ClipMax = (std::max)(s_ClipMax, pen);
            }
    }

    if ((m_AutoStep == 1 || m_AutoStep == 2) && m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        // high = 足元の生の高さマスが 2m 超（本当に台地の上）、hover = 生の地面より 0.5m 以上上（落ちている途中 /
        // 双線形の崖の裾に引っかかっている）
        int alive = 0, high = 0, falling = 0, hover = 0, nearCnt = 0;
        const Vector3 pp = tf.position;
        const float hs = GridWorld::kCellSize / (float)GridWorld::kHeightSub;
        if (m_Swarm.DebugReadEnemies(enemies, states))
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                ++alive;
                const Vector3 p = enemies[i].position;
                const float ground = gy + m_Grid.SampleHeight(p.x, p.z);
                const float raw = m_Grid.HeightAt((int)std::floor((p.x - m_Grid.OriginX()) / hs),
                                                  (int)std::floor((p.z - m_Grid.OriginZ()) / hs));
                if (raw > 2.0f) ++high;
                if (raw > 2.0f && s_Phase == 'A' && m_AutoTime - s_Start > 8.0f)
                {
                    // 降りて来ない 1 体の様子（原因を追う用）
                    char l2[192];
                    snprintf(l2, sizeof(l2), "drop stuck (%.2f,%.2f) y %.2f raw %.2f v (%.2f,%.2f,%.2f) anim %u player (%.2f,%.2f)",
                        p.x, p.z, p.y - gy, raw, enemies[i].velocity.x, enemies[i].velocity.y, enemies[i].velocity.z,
                        enemies[i].animIndex, pp.x, pp.z);
                    AutoTestLog(l2);
                }
                if (p.y > ground + 0.3f) ++falling;
                if (p.y - gy - raw > 0.5f) ++hover;
                if ((Vector3(p.x, 0, p.z) - Vector3(pp.x, 0, pp.z)).Length() < 3.0f) ++nearCnt;
            }
        // clip = この 0.5 秒の間（0.1 秒毎の読み戻し）に体へ崖の面が入っていた延べ数 / 一番深い m
        char line[160];
        snprintf(line, sizeof(line), "drop %c t %.1f alive %d high %d falling %d hover %d near %d clip %d/%.2f",
            s_Phase, m_AutoTime - s_Start, alive, high, falling, hover, nearCnt, s_ClipHits, s_ClipMax);
        AutoTestLog(line);
        s_ClipHits = 0;
        s_ClipMax = 0.0f;
    }
    const float t = m_AutoTime - s_Start;
    if (m_AutoStep == 1 && ((s_Looks == 0 && t >= 1.5f) || (s_Looks == 1 && t >= 2.5f) || (s_Looks == 2 && t >= 4.0f)))
    {
        char line[32];
        snprintf(line, sizeof(line), "drop look A %d", s_Looks++);
        AutoTestLog(line);
    }
    if (m_AutoStep == 2 && s_Looks == 0 && t >= 4.0f)
    {
        AutoTestLog("drop look B 0");
        ++s_Looks;
    }
}

REGISTER_BATTLE_AUTOTEST("drop", AutoTestDrop)
