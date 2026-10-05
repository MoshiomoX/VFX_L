// ============================================================
// AutoTestClip.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=clip
// VFXL_BATTLE_AUTOTEST=clip：プレイヤーを木 / 岩 / 外周 / 崖 / 台地の箱 / 崖（エリート）の横に立たせて雑魚を群がらせ、
// 体（半径）が塞がったマス・崖へどれだけ食い込むかを数えて撮る（2026-10-01、壁に嵌まって見える件の切り分け）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestClip final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 障害物の横での食い込み（VFXL_BATTLE_AUTOTEST=clip、2026-10-01）
// 「雑魚が壁に嵌まって見える」の切り分け。stuck は中心が塞がったマスに居るかしか数えないので、
// ここでは体の円（半径 enemyRadius × 体格）が塞がったマス / 崖（隣のマスとの高さの差 > 1.5m）へ
// どれだけ食い込むかを測る。シーン = 木（1 マスの塊）/ 岩（2〜6 マスの塊）/ 外周（北の縁）/
// 崖（地面のマスの隣が坂のある台地 / 高台）/ plateau（隣が坂の無い台地の箱）/ 崖 + エリート /
// climb（崖の台地の上に立ち、地面の雑魚が坂を登って来るか。high = 高さ 2m 超の数、16 秒）。各シーン: プレイヤーを障害物の隣のマスに立たせ（障害物寄りに 0.4m）、
// 周り 6〜11m に雑魚 30 体（エリートのシーンはエリート 8 体）。詠唱停止・無敵。
// 0.5 秒毎に `clip <シーン> t alive near inWall big n/max small n/max cliff n/max`
// （near = 障害物の中心から 3m 以内、n = 0.1m 超えて食い込んでいる数、max = 一番深い食い込み m）。
// 6 秒 `clip look <シーン> side`（横から）、8 秒 `clip look <シーン> top`（真上寄り）、外から撮る
// ============================================================
void AutoTestClip::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;

    struct Scene { const char* name; int ox, oz, px, pz; bool elite; bool ok; };
    static std::vector<Scene> s_Scenes;
    static int s_Index = 0;
    static float s_Start = 0.0f, s_NextLog = 0.0f;
    static int s_Phase = 0;
    static std::vector<int> s_Comp, s_CompSize;   // 塞がったマスの塊の番号 / 大きさ

    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    auto cellH = [&](int gx, int gz) { const Vector3 c = m_Grid.CellToWorld(gx, gz); return m_Grid.SampleHeight(c.x, c.z); };
    auto walk = [&](int gx, int gz) { return gx >= 0 && gz >= 0 && gx < W && gz < D && m_Grid.IsWalkable(gx, gz); };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        cam.avoidOcclusion = false;
        const Vector3 p0 = tf.position;
        int pgx, pgz;
        m_Grid.WorldToCell(p0, pgx, pgz);

        // 塞がったマスの塊（外周の 1 マスは除く、4 近傍）
        std::vector<int> comp((size_t)W * D, -1);
        std::vector<int> compSize;
        for (int z = 1; z < D - 1; ++z)
            for (int x = 1; x < W - 1; ++x)
            {
                if (m_Grid.IsWalkable(x, z) || comp[(size_t)z * W + x] >= 0) continue;
                const int id = (int)compSize.size();
                int n = 0;
                std::vector<std::pair<int, int>> st{ { x, z } };
                comp[(size_t)z * W + x] = id;
                while (!st.empty())
                {
                    auto [cx, cz] = st.back(); st.pop_back(); ++n;
                    const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (auto& d : nb)
                    {
                        const int nx = cx + d[0], nz = cz + d[1];
                        if (nx < 1 || nz < 1 || nx >= W - 1 || nz >= D - 1) continue;
                        if (m_Grid.IsWalkable(nx, nz) || comp[(size_t)nz * W + nx] >= 0) continue;
                        comp[(size_t)nz * W + nx] = id;
                        st.push_back({ nx, nz });
                    }
                }
                compSize.push_back(n);
            }

        // 障害物の隣で、プレイヤーが立てる平らなマス（周り 3x3 が歩けて高さが同じ）
        auto standCell = [&](int ox, int oz, int& sx, int& sz) -> bool
            {
                const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (auto& d : nb)
                {
                    const int x = ox + d[0], z = oz + d[1];
                    if (!walk(x, z)) continue;
                    const float h = cellH(x, z);
                    bool flat = true;
                    for (int dz = -1; dz <= 1 && flat; ++dz)
                        for (int dx = -1; dx <= 1 && flat; ++dx)
                        {
                            const int ax = x + dx, az = z + dz;
                            if (ax == ox && az == oz) continue;
                            if (walk(ax, az) && std::fabs(cellH(ax, az) - h) > 0.3f) flat = false;
                        }
                    if (flat) { sx = x; sz = z; return true; }
                }
                return false;
            };
        auto nearestBlob = [&](int minSize, int maxSize, Scene& sc)
            {
                float best = 1e30f;
                for (int z = 2; z < D - 2; ++z)
                    for (int x = 2; x < W - 2; ++x)
                    {
                        const int id = comp[(size_t)z * W + x];
                        if (id < 0 || compSize[id] < minSize || compSize[id] > maxSize) continue;
                        const float d2 = (float)((x - pgx) * (x - pgx) + (z - pgz) * (z - pgz));
                        if (d2 < 4.0f || d2 >= best) continue;
                        int sx, sz;
                        if (!standCell(x, z, sx, sz)) continue;
                        best = d2; sc.ox = x; sc.oz = z; sc.px = sx; sc.pz = sz; sc.ok = true;
                    }
            };

        Scene tree{ "tree", 0, 0, 0, 0, false, false };
        nearestBlob(1, 1, tree);
        Scene rock{ "rock", 0, 0, 0, 0, false, false };
        nearestBlob(2, 6, rock);

        Scene wall{ "wall", 0, 0, 0, 0, false, false };
        for (int k = 0; k < W / 2 && !wall.ok; ++k)
            for (int sgn = -1; sgn <= 1 && !wall.ok; sgn += 2)
            {
                const int x = W / 2 + sgn * k;
                if (walk(x, D - 2) && walk(x - 1, D - 2) && walk(x + 1, D - 2) && walk(x, D - 3) && cellH(x, D - 2) < 0.3f)
                { wall.ox = x; wall.oz = D - 1; wall.px = x; wall.pz = D - 2; wall.ok = true; }
            }

        // 崖：地面のマス（高さ < 0.3）で、4 近傍の 1 つが 2m 以上高い歩けるマス（坂のある台地・高台）。
        // plateau：同じく、4 近傍の 1 つが大きな塞がった塊（坂の無い台地の箱）
        auto findCliff = [&](bool blockedBlob, Scene& sc)
            {
                float best = 1e30f;
                for (int z = 3; z < D - 3; ++z)
                    for (int x = 3; x < W - 3; ++x)
                    {
                        if (!walk(x, z) || cellH(x, z) > 0.3f) continue;
                        const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                        for (auto& d : nb)
                        {
                            const int nx = x + d[0], nz = z + d[1];
                            const int id = comp[(size_t)nz * W + nx];
                            const bool high = blockedBlob ? (id >= 0 && compSize[id] > 6)
                                                          : (walk(nx, nz) && cellH(nx, nz) > 2.0f);
                            if (!high) continue;
                            // 反対側は平らな地面（群れが来られる）
                            if (!walk(x - d[0], z - d[1]) || cellH(x - d[0], z - d[1]) > 0.3f) continue;
                            const float d2 = (float)((x - pgx) * (x - pgx) + (z - pgz) * (z - pgz));
                            if (d2 < best) { best = d2; sc.ox = nx; sc.oz = nz; sc.px = x; sc.pz = z; sc.ok = true; }
                        }
                    }
            };
        Scene cliff{ "cliff", 0, 0, 0, 0, false, false };
        findCliff(false, cliff);
        Scene plateau{ "plateau", 0, 0, 0, 0, false, false };
        findCliff(true, plateau);
        Scene elite = cliff;
        elite.name = "elite";
        elite.elite = true;

        // climb：崖のシーンの台地の上に立つ（坂を登って来られるか。体の判定で坂に入れなくなっていないか）
        Scene climb{ "climb", cliff.px, cliff.pz, cliff.ox, cliff.oz, false, cliff.ok };

        s_Comp = comp;
        s_CompSize = compSize;
        s_Scenes = { tree, rock, wall, cliff, plateau, elite, climb };
        for (auto& sc : s_Scenes)
        {
            char line[160];
            const Vector3 o = m_Grid.CellToWorld(sc.ox, sc.oz);
            snprintf(line, sizeof(line), "clip scene %s ok %d obstacle (%d,%d) world (%.1f,%.1f) h %.2f stand (%d,%d) h %.2f",
                sc.name, sc.ok ? 1 : 0, sc.ox, sc.oz, o.x, o.z, sc.ok ? cellH(sc.ox, sc.oz) : 0.0f, sc.px, sc.pz,
                sc.ok ? cellH(sc.px, sc.pz) : 0.0f);
            AutoTestLog(line);
        }
        s_Index = -1;
        s_Phase = 9;   // 次のシーンへ
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    // ---- 次のシーンを始める ----
    if (s_Phase == 9)
    {
        ++s_Index;
        while (s_Index < (int)s_Scenes.size() && !s_Scenes[s_Index].ok) ++s_Index;
        if (s_Index >= (int)s_Scenes.size())
        {
            AutoTestLog("clip done");
            m_AutoStep = 2;
            return;
        }
        const Scene& sc = s_Scenes[s_Index];
        m_Swarm.KillAll();
        const Vector3 o = m_Grid.CellToWorld(sc.ox, sc.oz);
        const Vector3 c = m_Grid.CellToWorld(sc.px, sc.pz);
        Vector3 d(o.x - c.x, 0.0f, o.z - c.z);
        d.Normalize();
        const float px = c.x + d.x * 0.4f, pz = c.z + d.z * 0.4f;
        tf.position = Vector3(px, m_Grid.SampleHeight(px, pz) + 1.0f, pz);
        if (m_Registry.Has<RigidbodyComponent>(m_Player))
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;

        const float gy = m_Swarm.GetAIParams().groundY;
        const int count = sc.elite ? 8 : 30;
        const bool climbScene = (strcmp(sc.name, "climb") == 0);
        int spawned = 0;
        for (int k = 0; k < count * 4 && spawned < count; ++k)
        {
            const float a = (float)k * 2.399f;
            const float r = 6.0f + (float)(k % 6);
            const Vector3 p(px + std::cos(a) * r, gy, pz + std::sin(a) * r);
            int gx, gz;
            m_Grid.WorldToCell(p, gx, gz);
            if (!walk(gx, gz)) continue;
            if (climbScene && cellH(gx, gz) > 0.3f) continue;   // climb は地面にだけ出す（坂を登らせる）
            m_Swarm.SpawnEnemy(p, 100000.0f, 3.5f, sc.elite ? Swarm::kEnemyKindElite : Swarm::kEnemyKindMob);
            ++spawned;
        }

        // 横から：前方 = プレイヤー → 障害物 を 90 度回した向き（障害物とプレイヤーが左右に並ぶ）
        // FollowCamera: forward = (-sin(yaw), ., cos(yaw))
        const float fx = -d.z, fz = d.x;
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-fx, fz)));
        cam.SetPitch(25.0f);
        cam.distance = sc.elite ? 13.0f : 9.0f;
        cam.SnapToTarget();

        char line[96];
        snprintf(line, sizeof(line), "clip begin %s spawned %d", sc.name, spawned);
        AutoTestLog(line);
        s_Start = m_AutoTime;
        s_NextLog = m_AutoTime + 0.5f;
        s_Phase = 0;
        return;
    }

    const Scene& sc = s_Scenes[s_Index];
    const float t = m_AutoTime - s_Start;

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.5f;
        const float r = m_Swarm.GetAIParams().enemyRadius * (sc.elite ? m_Swarm.GetBomberParams().eliteScale : 1.0f);
        const Vector3 o = m_Grid.CellToWorld(sc.ox, sc.oz);
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        // big = 大きな塊（坂の無い台地の箱）と外周のマス：見た目の面がマスの縁と一致するので、マスへの食い込み = 見た目の食い込み。
        // small = 木・岩（1〜6 マス）：見た目は塞いだマスより小さい（木は幹がマスの中央）ので参考値。
        // cliff = 体の円周 16 方向へ 0.05m 刻みで高さ場を引き、足元より 0.5m 以上高い所（崖の面）が体の内側に入る深さ
        int alive = 0, nearCnt = 0, inWall = 0, bigPen = 0, smallPen = 0, cliffPen = 0, high = 0;
        char worst[160] = "";   // 崖に一番深く食い込んでいる 1 体（原因を追う用）
        float maxBig = 0.0f, maxSmall = 0.0f, maxCliff = 0.0f;
        if (m_Swarm.DebugReadEnemies(enemies, states))
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                ++alive;
                const Vector3 p = enemies[i].position;
                if ((Vector3(p.x, 0, p.z) - Vector3(o.x, 0, o.z)).Length() < 3.0f) ++nearCnt;
                if (m_Grid.SampleHeight(p.x, p.z) > 2.0f) ++high;
                int gx, gz;
                m_Grid.WorldToCell(p, gx, gz);
                if (!walk(gx, gz)) { ++inWall; continue; }
                float bp = 0.0f, sp = 0.0f, cp = 0.0f;
                const int reach = (int)std::ceil(r / GridWorld::kCellSize);
                for (int dz = -reach; dz <= reach; ++dz)
                    for (int dx = -reach; dx <= reach; ++dx)
                    {
                        if (dx == 0 && dz == 0) continue;
                        const int cx = gx + dx, cz = gz + dz;
                        if (cx < 0 || cz < 0 || cx >= W || cz >= D) continue;
                        if (m_Grid.IsWalkable(cx, cz)) continue;
                        const int id = s_Comp[(size_t)cz * W + cx];
                        const bool big = (id < 0) || s_CompSize[id] > 6;   // id < 0 = 外周
                        const Vector3 cc = m_Grid.CellToWorld(cx, cz);
                        const float hs = GridWorld::kCellSize * 0.5f;
                        const float qx = (std::max)(std::fabs(p.x - cc.x) - hs, 0.0f);
                        const float qz = (std::max)(std::fabs(p.z - cc.z) - hs, 0.0f);
                        const float depth = r - std::sqrt(qx * qx + qz * qz);
                        if (big) bp = (std::max)(bp, depth);
                        else     sp = (std::max)(sp, depth);
                    }
                // 高さは 0.5m の高さマスの生の値（双線形だと崖の 0.25m 手前から上がり始めるので、面に触れているだけでも食い込みに見える）
                auto rawH = [&](float x, float z)
                    {
                        const float s = GridWorld::kCellSize / (float)GridWorld::kHeightSub;
                        return m_Grid.HeightAt((int)std::floor((x - m_Grid.OriginX()) / s), (int)std::floor((z - m_Grid.OriginZ()) / s));
                    };
                // 基準は足の高さ（台地の縁から踏み出して縁の高さで支えられている途中 / 落ちている途中は、
                // 中心の下の地面より上に居る。地面基準だと背後の台地を食い込みと誤って数えた）
                const float h = (std::max)(rawH(p.x, p.z), p.y - m_Swarm.GetAIParams().groundY);
                for (int k = 0; k < 16; ++k)
                {
                    const float a = (float)k * DirectX::XM_2PI / 16.0f;
                    const float cx = std::cos(a), sz = std::sin(a);
                    for (float s = 0.05f; s <= r + 1e-4f; s += 0.05f)
                        if (rawH(p.x + cx * s, p.z + sz * s) - h > 0.8f)
                        {
                            if (r - s > maxCliff && r - s > cp)
                                snprintf(worst, sizeof(worst), " worst (%.2f,%.2f) h %.2f dir %d s %.2f h2 %.2f v (%.2f,%.2f) anim %u",
                                    p.x, p.z, h, k, s, rawH(p.x + cx * s, p.z + sz * s),
                                    enemies[i].velocity.x, enemies[i].velocity.z, enemies[i].animIndex);
                            cp = (std::max)(cp, r - s);
                            break;
                        }
                }
                if (bp > 0.1f) ++bigPen;
                if (sp > 0.1f) ++smallPen;
                if (cp > 0.1f) ++cliffPen;
                maxBig = (std::max)(maxBig, bp);
                maxSmall = (std::max)(maxSmall, sp);
                maxCliff = (std::max)(maxCliff, cp);
            }
        char line[400];
        snprintf(line, sizeof(line),
            "clip %s t %.1f alive %d near %d inWall %d big %d/%.2f small %d/%.2f cliff %d/%.2f high %d r %.2f%s",
            sc.name, t, alive, nearCnt, inWall, bigPen, maxBig, smallPen, maxSmall, cliffPen, maxCliff, high, r,
            maxCliff > 0.1f ? worst : "");
        AutoTestLog(line);
    }

    char line[64];
    const float t0 = (strcmp(sc.name, "climb") == 0) ? t - 10.0f : t;   // climb は坂を回って来るので 10 秒長く待つ
    if (s_Phase == 0 && t0 >= 6.0f) { snprintf(line, sizeof(line), "clip look %s side", sc.name); AutoTestLog(line); s_Phase = 1; }
    else if (s_Phase == 1 && t0 >= 6.6f) { cam.SetPitch(65.0f); cam.distance = sc.elite ? 16.0f : 11.0f; s_Phase = 2; }
    else if (s_Phase == 2 && t0 >= 8.0f) { snprintf(line, sizeof(line), "clip look %s top", sc.name); AutoTestLog(line); s_Phase = 3; }
    else if (s_Phase == 3 && t0 >= 8.6f) s_Phase = 9;
}

REGISTER_BATTLE_AUTOTEST("clip", AutoTestClip)
