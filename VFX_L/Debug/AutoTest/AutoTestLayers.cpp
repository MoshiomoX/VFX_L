// ============================================================
// AutoTestLayers.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=layers
// VFXL_BATTLE_AUTOTEST=layers：フィールドの三層（2026-10-02）。フローフィールドの作り直し時間、山頂・洞窟の俯瞰、
// 山頂の坂の上から滑り降り、洞窟の一番奥（Boss の門）、洞窟の入口、湧きを戻して雑魚の数
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestLayers final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: フィールドの三層（VFXL_BATTLE_AUTOTEST=layers、2026-10-02）
// 1 秒: 湧き停止・全消し・無敵。区域・坂・箱・門の位置と、フローフィールドの作り直し時間（打ち切り 0 / 40 / 60 / 80 マス）を記録。
// 以降、カメラを据えて記録 → 0.6 秒は動かさない（外から撮る）:
//   summit = 平原から山頂を俯瞰 / mine = 平原から洞窟を俯瞰 / ramp = 山頂の坂の上 →
//   滑り降り（0.25 秒毎に速さ・高さ）/ deep = 洞窟の一番奥の Boss の門 / entrance = 洞窟の入口から底 /
//   crowd = 中央へ戻して湧きを戻し 8 秒後の雑魚の数 → done
// ============================================================
void AutoTestLayers::Run(float dt)
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    const auto& lay = m_TerrainLayout;
    char line[256];
    static float s_Phase = 0.0f, s_Log = 0.0f;
    static Vector3 s_Dir;

    auto place = [&](const Vector3& p)
        {
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
            rb.velocity = Vector3::Zero;
        };
    auto look = [&](const Vector3& dir, float dist, float pitch)   // dir の向きを見る（カメラはプレイヤーの後ろ）
        {
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-dir.x, dir.z)));
            cam.distance = dist;
            cam.SetPitch(pitch);
            cam.SnapToTarget();
        };
    auto centroid = [&](const std::vector<int>& cells)
        {
            Vector3 c;
            for (int i : cells) c += m_Grid.CellToWorld(i % m_Grid.Width(), i / m_Grid.Width());
            return cells.empty() ? c : c / (float)cells.size();
        };
    auto stamp = [&](const char* what)
        {
            snprintf(line, sizeof(line), "layers look %s fps %.0f alive %u", what, ImGui::GetIO().Framerate,
                m_Swarm.GetCounters().aliveEnemies);
            AutoTestLog(line);
        };
    // 平原側から区域を見る：区域の重心から中央へ dist m の所に立ち、区域の方を向く
    auto overlook = [&](const std::vector<int>& cells, float fromCentroid, float camDist, float pitch)
        {
            const Vector3 c = centroid(cells);
            Vector3 toCenter = -c; toCenter.y = 0.0f; toCenter.Normalize();
            place(c + toCenter * fromCentroid);
            look(-toCenter, camDist, pitch);
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        cam.avoidOcclusion = false;

        const Vector3 sc = centroid(lay.summitCells), mc = centroid(lay.mineCells);
        snprintf(line, sizeof(line), "layers summit %d cells at %.0f,%.0f ramps %d / mine %d cells at %.0f,%.0f ramps %d deep %d (%.0f,%.1f,%.0f)",
            (int)lay.summitCells.size(), sc.x, sc.z, (int)lay.summitRamps.size(), (int)lay.mineCells.size(), mc.x, mc.z,
            (int)lay.mineRamps.size(), lay.hasMineDeep ? 1 : 0, lay.mineDeep.x, lay.mineDeep.y, lay.mineDeep.z);
        AutoTestLog(line);
        for (const auto& r : lay.summitRamps)
        {
            snprintf(line, sizeof(line), "layers summit ramp top %.0f,%.1f,%.0f down %.0f,%.0f",
                r.top.x, r.top.y, r.top.z, r.down.x, r.down.z);
            AutoTestLog(line);
        }
        for (const auto& r : lay.mineRamps)
        {
            snprintf(line, sizeof(line), "layers mine ramp top %.0f,%.1f,%.0f down %.0f,%.0f",
                r.top.x, r.top.y, r.top.z, r.down.x, r.down.z);
            AutoTestLog(line);
        }
        for (Entity e : m_Crates.GetCrates())
        {
            const Vector3 p = m_Registry.Get<TransformComponent>(e).position;
            snprintf(line, sizeof(line), "layers crate %.0f,%.1f,%.0f", p.x, p.y, p.z);
            AutoTestLog(line);
        }
        if (m_Registry.IsValid(m_Stage.GetPortal()))
        {
            const Vector3 p = m_Registry.Get<TransformComponent>(m_Stage.GetPortal()).position;
            snprintf(line, sizeof(line), "layers portal %.0f,%.1f,%.0f", p.x, p.y, p.z);
            AutoTestLog(line);
        }

        // フローフィールドの作り直し（中央・山頂の上から）。打ち切りを変えて測る。
        // 本体の場で直接作る（ゲーム中の作り直しは別スレッドの作業用の場なので、混ざらない。
        // 本体の結果は次の作業用の結果で上書きされ、GPU へは上げていない）
        FlowField& flow = m_Swarm.GetFlowField();
        const int keep = flow.maxRangeCells;
        int cx = 0, cz = 0, sx = 0, sz = 0;
        m_Grid.WorldToCell(Vector3::Zero, cx, cz);
        if (!lay.summitCells.empty()) { sx = lay.summitCells[0] % m_Grid.Width(); sz = lay.summitCells[0] / m_Grid.Width(); }
        for (int range : { 0, 40, 60, 80 })
        {
            flow.maxRangeCells = range;
            double ms[2] = {};
            for (int k = 0; k < 2; ++k)
            {
                const auto t0 = std::chrono::high_resolution_clock::now();
                for (int rep = 0; rep < 5; ++rep) flow.Build(k == 0 ? cx : sx, k == 0 ? cz : sz);
                ms[k] = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count() / 5.0;
            }
            snprintf(line, sizeof(line), "layers flow range %d build ms center %.2f summit %.2f", range, ms[0], ms[1]);
            AutoTestLog(line);
        }
        flow.maxRangeCells = keep;

        overlook(lay.summitCells, 65.0f, 40.0f, 22.0f);
        m_AutoStep = 1;
        // VFXL_LAYERS_CLIMB=1 なら登りの段だけ
        char env[8] = {};
        if (GetEnvironmentVariableA("VFXL_LAYERS_CLIMB", env, sizeof(env)) > 0)
        {
            m_AutoStep = 13;
            s_Phase = m_AutoTime - 13.0f;
        }
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 3.0f) { stamp("summit"); m_AutoStep = 2; }
    else if (m_AutoStep == 2 && m_AutoTime >= 3.6f) { overlook(lay.mineCells, 70.0f, 45.0f, 16.0f); m_AutoStep = 3; }
    else if (m_AutoStep == 3 && m_AutoTime >= 5.5f) { stamp("mine"); m_AutoStep = 4; }
    else if (m_AutoStep == 4 && m_AutoTime >= 6.1f)
    {
        if (lay.summitRamps.empty()) { AutoTestLog("layers no summit ramp"); m_AutoStep = 8; return; }
        const auto& r = lay.summitRamps[0];
        s_Dir = r.down;
        place(r.top - r.down * 3.0f);
        cam.avoidOcclusion = true;
        look(r.down, 9.0f, 18.0f);
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5 && m_AutoTime >= 7.5f) { stamp("ramp"); s_Phase = s_Log = 0.0f; m_AutoStep = 6; }
    else if (m_AutoStep == 6 && m_AutoTime >= 8.1f)
    {
        // 坂の下へ走り、0.4 秒後から滑る
        s_Phase += dt;
        Vector3 camF = cam.GetForward(); camF.y = 0; camF.Normalize();
        Vector3 camR = cam.GetRight();   camR.y = 0; camR.Normalize();
        pcs.testInput = true;
        pcs.testMove = Vector2(s_Dir.Dot(camR), s_Dir.Dot(camF));
        pcs.testJump = false;
        pcs.testSlide = s_Phase >= 0.4f;
        s_Log += dt;
        if (s_Log >= 0.25f)
        {
            s_Log = 0.0f;
            const float hs = std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
            const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            snprintf(line, sizeof(line), "layers slide t %.2f speed %.2f y %.2f grounded %d sliding %d",
                s_Phase, hs, tf.position.y, rb.isGrounded ? 1 : 0, st.slideActive ? 1 : 0);
            AutoTestLog(line);
        }
        if (s_Phase >= 2.0f && s_Phase - dt < 2.0f) stamp("slide");
        if (s_Phase >= 6.0f)
        {
            pcs.testInput = false;
            pcs.testSlide = false;
            m_AutoStep = 7;
        }
    }
    else if (m_AutoStep == 7)
    {
        // 洞窟の一番奥：門の手前 7m（中央寄り）から門を見る
        Vector3 target = lay.mineDeep;
        if (m_Registry.IsValid(m_Stage.GetPortal()))
            target = m_Registry.Get<TransformComponent>(m_Stage.GetPortal()).position;
        // 門から洞窟の重心の方へ 9m（坑の中）に立ち、門を見る
        Vector3 inward = centroid(lay.mineCells) - target; inward.y = 0.0f;
        if (inward.LengthSquared() < 1.0f) inward = -target;
        inward.y = 0.0f; inward.Normalize();
        place(target + inward * 9.0f);
        look(-inward, 12.0f, 32.0f);
        s_Phase = m_AutoTime;
        m_AutoStep = 8;
    }
    else if (m_AutoStep == 8 && m_AutoTime >= s_Phase + 1.5f) { stamp("deep"); m_AutoStep = 9; }
    else if (m_AutoStep == 9 && m_AutoTime >= s_Phase + 2.1f)
    {
        if (!lay.mineRamps.empty())
        {
            const auto& r = lay.mineRamps[0];
            place(r.top - r.down * 5.0f);
            look(r.down, 10.0f, 30.0f);
        }
        m_AutoStep = 10;
    }
    else if (m_AutoStep == 10 && m_AutoTime >= s_Phase + 3.6f) { stamp("entrance"); m_AutoStep = 11; }
    else if (m_AutoStep == 11 && m_AutoTime >= s_Phase + 4.2f)
    {
        // 中央へ戻して湧きを戻す
        place(Vector3::Zero);
        cam.distance = 30.0f;
        cam.SetPitch(40.0f);
        cam.SnapToTarget();
        m_Mobs.Director().enabled = true;
        m_AutoStep = 12;
    }
    else if (m_AutoStep == 12 && m_AutoTime >= s_Phase + 12.2f) { stamp("crowd"); m_AutoStep = 13; }
    else if (m_AutoStep == 13 && m_AutoTime >= s_Phase + 13.0f)
    {
        // 登り：プレイヤーを山頂の内側寄り（重心から中央へ 20m の辺りの広い所）へ、崖の下の平原（14〜40m）に
        // 打たれ強い雑魚 30 体。坂へ回って登って来るか
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        Vector3 sc = centroid(lay.summitCells);
        {
            Vector3 toCenter = -sc; toCenter.y = 0.0f; toCenter.Normalize();
            sc += toCenter * 20.0f;
        }
        Vector3 top = sc;
        float bestD = 1e9f;
        for (int i : lay.summitCells)
        {
            const int gx = i % m_Grid.Width(), gz = i / m_Grid.Width();
            bool roomy = true;
            for (int dz = -2; dz <= 2 && roomy; ++dz)
                for (int dx = -2; dx <= 2 && roomy; ++dx)
                    roomy = m_Grid.IsWalkable(gx + dx, gz + dz);
            const Vector3 c = m_Grid.CellToWorld(gx, gz);
            const float d = (c - sc).LengthSquared();
            if (roomy && d < bestD) { bestD = d; top = c; }
        }
        place(top);
        cam.distance = 30.0f;
        cam.SetPitch(50.0f);
        cam.SnapToTarget();
        const float gy = m_Swarm.GetAIParams().groundY;
        int spawned = 0;
        for (int attempt = 0; attempt < 2000 && spawned < 30; ++attempt)
        {
            const float a = (float)attempt * 2.399963f;   // 黄金角で散らす
            const float r = 14.0f + (float)(attempt % 27);
            const Vector3 p = top + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
            int gx, gz;
            m_Grid.WorldToCell(p, gx, gz);
            if (!m_Grid.IsWalkable(gx, gz) || std::fabs(m_Grid.SampleHeight(p.x, p.z)) > 0.1f) continue;
            m_Swarm.SpawnEnemy({ p.x, gy, p.z }, 100000.0f, 8.0f, Swarm::kEnemyKindMob);
            ++spawned;
        }
        snprintf(line, sizeof(line), "layers climb start (flow range %d): player on summit at %.0f,%.1f,%.0f, %d mobs on the plain",
            m_Swarm.GetFlowField().maxRangeCells, top.x, tf.position.y, top.z, spawned);
        AutoTestLog(line);
        s_Phase = m_AutoTime;
        s_Log = 0.0f;
        m_AutoStep = 14;
    }
    else if (m_AutoStep == 14)
    {
        s_Log += dt;
        if (s_Log >= 2.0f)
        {
            s_Log = 0.0f;
            std::vector<Swarm::Enemy> enemies;
            std::vector<uint32_t> states;
            int alive = 0, onTop = 0, close = 0, plain = 0;   // near は windef.h のマクロ
            const Vector3 pp = tf.position;
            if (m_Swarm.DebugReadEnemies(enemies, states))
                for (size_t i = 0; i < enemies.size(); ++i)
                {
                    if (states[i] == Swarm::kStateDead) continue;
                    ++alive;
                    const Vector3 p = enemies[i].position;
                    const float h = m_Grid.SampleHeight(p.x, p.z);
                    if (h > m_TerrainConfig.summitHeight - 1.0f) ++onTop;
                    else if (h < 0.5f) ++plain;
                    if ((Vector3(p.x, 0, p.z) - Vector3(pp.x, 0, pp.z)).Length() < 8.0f) ++close;
                }
            snprintf(line, sizeof(line), "layers climb t %.0f alive %d onSummit %d onPlain %d within8m %d",
                m_AutoTime - s_Phase, alive, onTop, plain, close);
            AutoTestLog(line);

            // 診断：最初の 3 体の位置・マスの流れの向き・コスト、CPU のフローフィールドを辿って目標に着くか
            const FlowField& flow = m_Swarm.GetFlowField();
            const auto& dirs = flow.Directions();
            const auto& costs = flow.Costs();
            const int W = m_Grid.Width();
            int shown = 0;
            for (size_t i = 0; i < enemies.size() && shown < 3 && m_AutoTime - s_Phase < 7.0f; ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                const Vector3 p = enemies[i].position;
                int gx, gz;
                m_Grid.WorldToCell(p, gx, gz);
                const int c = gz * W + gx;
                if (dirs.empty() || c < 0 || c >= (int)dirs.size()) continue;
                // 向きを辿る（最大 400 歩）
                int x = gx, z = gz, steps = 0;
                float maxH = m_Grid.SampleHeight(p.x, p.z);
                while (steps < 400 && !(x == flow.TargetX() && z == flow.TargetZ()))
                {
                    const DirectX::SimpleMath::Vector2 d = dirs[(size_t)z * W + x];
                    if (d.x * d.x + d.y * d.y < 0.01f) break;
                    x += (d.x > 0.3f) ? 1 : (d.x < -0.3f) ? -1 : 0;
                    z += (d.y > 0.3f) ? 1 : (d.y < -0.3f) ? -1 : 0;
                    const Vector3 w = m_Grid.CellToWorld(x, z);
                    maxH = (std::max)(maxH, m_Grid.SampleHeight(w.x, w.z));
                    ++steps;
                }
                snprintf(line, sizeof(line), "layers climb mob %d at %.1f,%.1f,%.1f vel %.1f,%.1f cell dir %.2f,%.2f cost %.0f | trace %d steps to %d,%d (target %d,%d) maxH %.1f",
                    shown, p.x, p.y, p.z, enemies[i].velocity.x, enemies[i].velocity.z, dirs[(size_t)c].x, dirs[(size_t)c].y,
                    costs.empty() ? -1.0f : (costs[(size_t)c] > 1e30f ? -1.0f : costs[(size_t)c]),
                    steps, x, z, flow.TargetX(), flow.TargetZ(), maxH);
                AutoTestLog(line);
                ++shown;
            }
        }
        // 1 回目 = 今のフローフィールドの設定（全域）、2 回目 = 打ち切り 40 マスで比べる
        static int s_Round = 0;
        if (m_AutoTime >= s_Phase + 14.0f && m_AutoTime - dt < s_Phase + 14.0f) stamp(s_Round == 0 ? "climb" : "climb40");
        if (m_AutoTime >= s_Phase + 30.5f)
        {
            FlowField& flow = m_Swarm.GetFlowField();
            if (s_Round == 0)
            {
                s_Round = 1;
                flow.maxRangeCells = 40;
                m_Swarm.RequestFlowRebuild();
                m_AutoStep = 13;   // 同じ所でもう一度
                s_Phase = m_AutoTime - 13.0f;
            }
            else
            {
                flow.maxRangeCells = 0;
                m_Swarm.RequestFlowRebuild();
                m_AutoStep = 16;   // 洞へ
            }
        }
    }
    else if (m_AutoStep == 16)
    {
        // 洞：プレイヤーを洞窟の一番奥へ、坑の外の平原（プレイヤーから 20〜60m）に雑魚 30 体。口（下り坂）から入って来るか
        m_Swarm.KillAll();
        if (!lay.hasMineDeep) { AutoTestLog("layers done (no mine)"); m_AutoStep = 18; return; }
        place(lay.mineDeep);
        cam.distance = 10.0f;
        cam.SetPitch(30.0f);
        cam.SnapToTarget();
        const float gy = m_Swarm.GetAIParams().groundY;
        int spawned = 0;
        for (int attempt = 0; attempt < 4000 && spawned < 30; ++attempt)
        {
            const float a = (float)attempt * 2.399963f;
            const float r = 20.0f + (float)(attempt % 41);
            const Vector3 p = lay.mineDeep + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
            int gx, gz;
            m_Grid.WorldToCell(p, gx, gz);
            if (!m_Grid.IsWalkable(gx, gz) || std::fabs(m_Grid.SampleHeight(p.x, p.z)) > 0.1f) continue;
            m_Swarm.SpawnEnemy({ p.x, gy, p.z }, 100000.0f, 8.0f, Swarm::kEnemyKindMob);
            ++spawned;
        }
        snprintf(line, sizeof(line), "layers cave start: player at %.0f,%.1f,%.0f, %d mobs on the plain",
            tf.position.x, tf.position.y, tf.position.z, spawned);
        AutoTestLog(line);
        s_Phase = m_AutoTime;
        s_Log = 0.0f;
        m_AutoStep = 17;
    }
    else if (m_AutoStep == 17)
    {
        s_Log += dt;
        if (s_Log >= 2.0f)
        {
            s_Log = 0.0f;
            std::vector<Swarm::Enemy> enemies;
            std::vector<uint32_t> states;
            int alive = 0, inCave = 0, close = 0, inWall = 0;
            const Vector3 pp = tf.position;
            if (m_Swarm.DebugReadEnemies(enemies, states))
                for (size_t i = 0; i < enemies.size(); ++i)
                {
                    if (states[i] == Swarm::kStateDead) continue;
                    ++alive;
                    const Vector3 p = enemies[i].position;
                    if (m_Grid.SampleHeight(p.x, p.z) < -5.0f) ++inCave;
                    if ((Vector3(p.x, 0, p.z) - Vector3(pp.x, 0, pp.z)).Length() < 8.0f) ++close;
                    int gx, gz;
                    m_Grid.WorldToCell(p, gx, gz);
                    if (!m_Grid.IsWalkable(gx, gz)) ++inWall;
                }
            snprintf(line, sizeof(line), "layers cave t %.0f alive %d inCave %d within8m %d inWall %d",
                m_AutoTime - s_Phase, alive, inCave, close, inWall);
            AutoTestLog(line);
        }
        if (m_AutoTime >= s_Phase + 12.0f && m_AutoTime - dt < s_Phase + 12.0f) stamp("cave");
        if (m_AutoTime >= s_Phase + 30.5f)
        {
            AutoTestLog("layers done");
            m_AutoStep = 18;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("layers", AutoTestLayers)
