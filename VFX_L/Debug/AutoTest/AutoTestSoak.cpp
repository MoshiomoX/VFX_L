// ============================================================
// AutoTestSoak.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=soak
// VFXL_BATTLE_AUTOTEST=soak：普通に湧かせて 10 分回し（VFXL_SOAK_MIN）、プレイヤーは場所を巡る。
// 0.25 秒毎に敵の池を読み戻し、めり込み（壁・崖・地面）と経路探索（詰まり・その場で回る・道に沿って進まない）を数える
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestSoak final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 実時間の通し検査（VFXL_BATTLE_AUTOTEST=soak、2026-10-03）
// ユーザー：30 分ほど実際に回して、めり込みと経路探索（詰まる・その場で回る）を見て、あれば直して報告。
// 普通に湧かせ（Director のまま）、プレイヤーは無敵・MP 満タン・経験 0（4 択を出さない）、バックパックは追尾弾 + 火球 + 毒。
// プレイヤーは 50 秒毎に場所を変える（平原 / 山頂 / 洞窟の奥 / 台地・高台の上 / 外周の近く / 林の横 / 山頂の坂の上 /
// 洞窟の入口の外 / 洞窟の底）。前半 25 秒は立ち止まり（雑魚が集まり切るか = 経路探索を見る）、後半は半径 6m の円を歩く。
// 0.25 秒毎に敵の池を全部読み戻して（幽霊は壁を抜ける仕様なので除く）:
//   めり込み: inWall = 中心が塞がったマス / wallPen = 体（壁用の半径）が塞がったマスへ（大きい塊 0.15m・木岩 0.3m 以上）/
//         cliffPen = 崖へ 0.15m 以上 / sink = 足が地面より 0.3m 以上下 / hover = 体の下の一番高い地面より 0.6m 以上上で
//         落下中でない / oob = フィールドの外 / bigVis = エリート・Boss の見た目の半径が大きい壁・崖へ 0.3m 以上
//         （壁用の半径は 0.9m で頭打ちなので、見た目は食い込み得る）/ playerSink・playerWall = プレイヤー
//   経路探索（立ち止まって 9 秒後から = 8 秒の窓がプレイヤーの移動の後。プレイヤーから 6m 以上、周り 3x3 マスに 8 体未満、硬直でない時だけ）:
//         unreach = フローフィールドで届かないマス / stuck = 8 秒で道のり 1m 未満 /
//         spin = 8 秒で道のり 3.5m 以上なのに正味 1m 未満、または向きが 720 度以上回って正味 2m 未満 /
//         noProg = フローフィールドの代価（≒マス数）が 8 秒で 1 も減らない（道に沿って進めていない）
// 同じスロット・種類は 10 秒空くまで 1 件（episode）。各種類 40 件まで `soak bad ...` に詳細、
// 種類毎に最初と以後 90 秒毎にカメラをそちらへ向けて `soak look <種類> <n>`（外から撮る。0.7 秒後に戻す）。
// 10 秒毎に `soak sum`。経過 70% で計時を時間切れの 10 秒前へ（最終ウェーブ・幽霊）、80% で洞窟の奥の門から Boss。
// VFXL_SOAK_MIN（既定 10）分で `soak total` と `soak done`
// ============================================================
void AutoTestSoak::Run(float dt)
{
    enum Bad { kInWall, kWallPen, kCliffPen, kSink, kHover, kOob, kBigVis, kPropClip, kStuck, kSpin, kNoProg, kUnreach,
               kPlayerSink, kPlayerWall, kBadCount };
    static const char* kBadName[kBadCount] = { "inWall", "wallPen", "cliffPen", "sink", "hover", "oob", "bigVis",
        "propClip", "stuck", "spin", "noProg", "unreach", "playerSink", "playerWall" };
    enum SpotKind { kPlain, kSummit, kMineDeep, kPlateau, kEdge, kWoods, kSummitRamp, kMineMouth, kMineFloor, kSpotKinds };
    static const char* kSpotName[kSpotKinds] = { "plain", "summit", "mineDeep", "plateau", "edge", "woods",
        "summitRamp", "mineMouth", "mineFloor" };
    constexpr int kHist = 33;             // 0.25 秒毎 × 32 = 8 秒
    constexpr float kSampleDt = 0.25f;
    constexpr float kSpotTime = 50.0f;    // 1 か所の長さ
    constexpr float kStandTime = 25.0f;   // 前半は立ち止まる
    struct Track { float x[kHist], z[kHist], yaw[kHist], cost[kHist]; int count = 0, head = 0; uint32_t kind = 0; };
    struct Spot { Vector3 p; int kind = kPlain; };

    static float s_Total = 600.0f, s_NextSample = 0.0f, s_NextSum = 0.0f, s_SpotStart = -1000.0f;
    static float s_Angle = 0.0f, s_CamBack = -1.0f, s_LookAt = -1.0f;
    static float s_SavedYaw = 0.0f, s_SavedPitch = 28.0f, s_SavedDist = 8.0f;
    static char s_LookLine[200] = {};
    static int s_SpotIdx = -1, s_Looks = 0;
    static bool s_FinalDone = false, s_BossDone = false, s_BossPending = false;
    static std::vector<Track> s_Tracks;
    static std::vector<float> s_LastBad;
    static float s_PlayerLastBad[kBadCount] = {};
    static std::vector<int> s_Comp, s_CompSize;
    static std::vector<uint8_t> s_Zone;            // 0 平原 / 1 山頂 / 2 洞窟
    static std::vector<uint16_t> s_Dens;
    static std::vector<std::vector<Spot>> s_Spots;
    static Spot s_Cur;
    static int s_Episodes[kBadCount] = {}, s_Now[kBadCount] = {};
    static float s_Max[kBadCount] = {}, s_NextLook[kBadCount] = {};
    static std::mt19937 s_Rng;
    static std::vector<Swarm::Enemy> s_Enemies;
    static std::vector<uint32_t> s_States;
    static std::vector<Swarm::EnemyExtra> s_Extras;

    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    auto& cam = m_Camera.Camera();
    auto& pcs = m_PlayerControlSystem;
    const auto& lay = m_TerrainLayout;
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    const float t = m_AutoTime;
    char line[1100];

    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.current = mp.max;
    }
    if (m_AutoStep >= 2) return;

    auto cellH = [&](int x, int z)
        {
            const Vector3 c = m_Grid.CellToWorld(x, z);
            return m_Grid.SampleHeight(c.x, c.z);
        };
    // 0.5m の高さマスの生の値（双線形だと崖の 0.25m 手前から上がり始める）
    auto rawH = [&](float x, float z)
        {
            const float s = GridWorld::kCellSize / (float)GridWorld::kHeightSub;
            return m_Grid.HeightAt((int)std::floor((x - m_Grid.OriginX()) / s), (int)std::floor((z - m_Grid.OriginZ()) / s));
        };
    auto bigBlocked = [&](int x, int z)
        {
            const int id = s_Comp[(size_t)z * W + x];
            return id < 0 || s_CompSize[id] > 6;
        };

    // ---------- 準備 ----------
    if (m_AutoStep == 0)
    {
        if (t < 1.0f) return;
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_SOAK_MIN", env, sizeof(env)) > 0)
            s_Total = (std::max)(1.0f, (float)atof(env)) * 60.0f;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        s_Rng.seed(m_TerrainConfig.seed * 2654435761u + 7u);
        s_Tracks.assign(Swarm::kMaxEnemies, Track{});
        s_LastBad.assign((size_t)Swarm::kMaxEnemies * kBadCount, -100.0f);
        for (int k = 0; k < kBadCount; ++k)
        {
            s_Episodes[k] = 0; s_Max[k] = 0.0f; s_NextLook[k] = 0.0f; s_PlayerLastBad[k] = -100.0f;
        }
        s_Dens.assign((size_t)W * D, 0);

        // バックパック：開始時の追尾弾 + 火球 + 毒（雑魚を減らし過ぎない程度）
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo + 1, 0);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            BackpackLogic::Place(bp, ItemID::Poison, lo + 2, lo + 2, 0);
        }

        // 塞がったマスの塊（6 マス以下 = 木・岩。大きい塊と外周 = 見える壁）
        s_Comp.assign((size_t)W * D, -1);
        s_CompSize.clear();
        for (int z = 0; z < D; ++z)
            for (int x = 0; x < W; ++x)
            {
                if (m_Grid.IsWalkable(x, z) || s_Comp[(size_t)z * W + x] >= 0) continue;
                const int id = (int)s_CompSize.size();
                std::vector<int> q{ z * W + x };
                s_Comp[(size_t)z * W + x] = id;
                for (size_t qi = 0; qi < q.size(); ++qi)
                {
                    const int cx = q[qi] % W, cz = q[qi] / W;
                    const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (auto& d : nb)
                    {
                        const int nx = cx + d[0], nz = cz + d[1];
                        if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                        if (m_Grid.IsWalkable(nx, nz) || s_Comp[(size_t)nz * W + nx] >= 0) continue;
                        s_Comp[(size_t)nz * W + nx] = id;
                        q.push_back(nz * W + nx);
                    }
                }
                s_CompSize.push_back((int)q.size());
            }

        // 区域と立つ場所の候補（3x3 が歩けて平らなマス）
        s_Zone.assign((size_t)W * D, 0);
        for (int c : lay.summitCells) s_Zone[(size_t)c] = 1;
        for (int c : lay.mineCells) s_Zone[(size_t)c] = 2;
        auto flat = [&](int x, int z)
            {
                if (x < 1 || z < 1 || x >= W - 1 || z >= D - 1) return false;
                const float h = cellH(x, z);
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (!m_Grid.IsWalkable(x + dx, z + dz) || std::fabs(cellH(x + dx, z + dz) - h) > 0.15f) return false;
                return true;
            };
        s_Spots.assign(kSpotKinds, {});
        for (int z = 0; z < D; ++z)
            for (int x = 0; x < W; ++x)
            {
                if (!flat(x, z)) continue;
                const float h = cellH(x, z);
                Vector3 p = m_Grid.CellToWorld(x, z);
                p.y = h;
                const uint8_t zone = s_Zone[(size_t)z * W + x];
                if (zone == 1) s_Spots[kSummit].push_back({ p, kSummit });
                else if (zone == 2) s_Spots[kMineFloor].push_back({ p, kMineFloor });
                else if (std::fabs(h) < 0.1f)
                {
                    s_Spots[kPlain].push_back({ p, kPlain });
                    if (x <= 4 || z <= 4 || x >= W - 5 || z >= D - 5) s_Spots[kEdge].push_back({ p, kEdge });
                    bool woods = false;
                    for (int dz = -2; dz <= 2 && !woods; ++dz)
                        for (int dx = -2; dx <= 2 && !woods; ++dx)
                        {
                            const int nx = x + dx, nz = z + dz;
                            if (nx < 0 || nz < 0 || nx >= W || nz >= D || m_Grid.IsWalkable(nx, nz)) continue;
                            woods = !bigBlocked(nx, nz);
                        }
                    if (woods) s_Spots[kWoods].push_back({ p, kWoods });
                }
                else if (h > 1.5f && h < 13.0f) s_Spots[kPlateau].push_back({ p, kPlateau });
            }
        if (lay.hasMineDeep) s_Spots[kMineDeep].push_back({ lay.mineDeep, kMineDeep });
        for (const auto& r : lay.summitRamps) s_Spots[kSummitRamp].push_back({ r.top - r.down * 2.0f, kSummitRamp });
        for (const auto& r : lay.mineRamps) s_Spots[kMineMouth].push_back({ r.top - r.down * 4.0f, kMineMouth });

        snprintf(line, sizeof(line), "soak start seed %u total %.0f s spots plain %zu summit %zu mineDeep %zu plateau %zu edge %zu woods %zu summitRamp %zu mineMouth %zu mineFloor %zu comps %zu",
            m_TerrainConfig.seed, s_Total, s_Spots[kPlain].size(), s_Spots[kSummit].size(), s_Spots[kMineDeep].size(),
            s_Spots[kPlateau].size(), s_Spots[kEdge].size(), s_Spots[kWoods].size(), s_Spots[kSummitRamp].size(),
            s_Spots[kMineMouth].size(), s_Spots[kMineFloor].size(), s_CompSize.size());
        AutoTestLog(line);
        s_NextSample = s_NextSum = t;
        s_SpotStart = -1000.0f;
        s_SpotIdx = -1;
        m_AutoStep = 1;
    }

    // ---------- 最終ウェーブ・Boss ----------
    if (!s_FinalDone && t >= s_Total * 0.7f)
    {
        m_RunTime = (std::max)(m_RunTime, m_Stage.stageTime - 10.0f);
        s_FinalDone = true;
        AutoTestLog("soak final wave (run time -> stage time - 10 s)");
    }
    const bool bossNow = !s_BossDone && t >= s_Total * 0.8f;

    // ---------- 場所を変える ----------
    if (t - s_SpotStart >= kSpotTime || bossNow)
    {
        if (bossNow)
        {
            s_BossDone = true;
            const float yaw = DirectX::XMConvertToRadians(m_Stage.GetPortalYaw());
            s_Cur = { m_Stage.GetPortalCenter() + Vector3(std::sin(yaw), 0.0f, std::cos(yaw)) * 2.0f, kMineDeep };
            s_BossPending = true;
        }
        else
        {
            static const int kOrder[] = { kPlain, kSummit, kMineDeep, kPlateau, kEdge, kWoods, kSummitRamp, kMineMouth, kMineFloor };
            ++s_SpotIdx;
            int kind = kOrder[s_SpotIdx % (int)std::size(kOrder)];
            if (s_Spots[kind].empty()) kind = kPlain;
            const auto& list = s_Spots[kind];
            s_Cur = list[std::uniform_int_distribution<int>(0, (int)list.size() - 1)(s_Rng)];
        }
        tf.position = Vector3(s_Cur.p.x, m_Grid.SampleHeight(s_Cur.p.x, s_Cur.p.z) + 1.0f, s_Cur.p.z);
        rb.velocity = Vector3::Zero;
        if (s_CamBack > 0.0f) { cam.SetYaw(s_SavedYaw); cam.SetPitch(s_SavedPitch); cam.distance = s_SavedDist; s_CamBack = -1.0f; }
        s_LookAt = -1.0f;
        cam.SnapToTarget();
        s_SpotStart = t;
        s_Angle = std::uniform_real_distribution<float>(0.0f, DirectX::XM_2PI)(s_Rng);
        snprintf(line, sizeof(line), "soak spot %d %s%s at %.1f,%.1f,%.1f", s_SpotIdx, kSpotName[s_Cur.kind],
            bossNow ? " (boss)" : "", tf.position.x, tf.position.y, tf.position.z);
        AutoTestLog(line);
    }
    if (s_BossPending && t - s_SpotStart > 0.5f)
    {
        m_AutoInteract = true;   // 門の前で F
        s_BossPending = false;
        AutoTestLog("soak boss summon");
    }

    // ---------- プレイヤーの動き ----------
    const bool standing = (t - s_SpotStart) < kStandTime;
    {
        Vector3 camF = cam.GetForward(); camF.y = 0.0f; camF.Normalize();
        Vector3 camR = cam.GetRight();   camR.y = 0.0f; camR.Normalize();
        pcs.testInput = true;
        pcs.testSlide = false;
        pcs.testJump = false;
        if (standing) pcs.testMove = Vector2::Zero;
        else
        {
            s_Angle += dt * 0.8f;
            const Vector3 goal = s_Cur.p + Vector3(std::cos(s_Angle + 0.6f), 0.0f, std::sin(s_Angle + 0.6f)) * 6.0f;
            Vector3 d = goal - tf.position;
            d.y = 0.0f;
            if (d.LengthSquared() > 1e-4f) d.Normalize();
            pcs.testMove = Vector2(d.Dot(camR), d.Dot(camF));
        }
    }

    // ---------- 撮影（カメラを異常の方へ向けて、戻す）----------
    if (s_LookAt > 0.0f && t >= s_LookAt) { AutoTestLog(s_LookLine); s_LookAt = -1.0f; s_CamBack = t + 0.7f; }
    if (s_CamBack > 0.0f && t >= s_CamBack)
    {
        cam.SetYaw(s_SavedYaw); cam.SetPitch(s_SavedPitch); cam.distance = s_SavedDist;
        s_CamBack = -1.0f;
    }
    auto lookAt = [&](int type, const Vector3& q)
        {
            if (s_LookAt > 0.0f || s_CamBack > 0.0f || s_Looks >= 30 || t < s_NextLook[type]) return;
            s_NextLook[type] = t + 90.0f;
            ++s_Looks;
            s_SavedYaw = cam.GetYaw(); s_SavedPitch = cam.GetPitch(); s_SavedDist = cam.distance;
            Vector3 d = q - tf.position;
            d.y = 0.0f;
            const float L = d.Length();
            if (L > 0.5f) cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-d.x, d.z)));
            cam.distance = std::clamp(L * 0.6f + 6.0f, 8.0f, 24.0f);
            cam.SetPitch(L > 15.0f ? 40.0f : 30.0f);
            cam.SnapToTarget();
            snprintf(s_LookLine, sizeof(s_LookLine), "soak look %s %d at %.1f,%.1f,%.1f dist %.1f",
                kBadName[type], s_Looks, q.x, q.y, q.z, L);
            s_LookAt = t + 0.4f;
        };

    // ---------- 終わり ----------
    if (t >= s_Total)
    {
        std::string s = "soak total episodes";
        for (int k = 0; k < kBadCount; ++k)
        {
            char b[64];
            snprintf(b, sizeof(b), " %s %d/%.2f", kBadName[k], s_Episodes[k], s_Max[k]);
            s += b;
        }
        AutoTestLog(s.c_str());
        AutoTestLog("soak done");
        pcs.testInput = false;
        m_AutoStep = 2;
        return;
    }

    // ---------- 読み戻して数える ----------
    if (t < s_NextSample) return;
    s_NextSample = t + kSampleDt;
    if (!m_Swarm.DebugReadEnemies(s_Enemies, s_States, &s_Extras)) return;

    const auto& bomber = m_Swarm.GetBomberParams();
    const float gy = m_Swarm.GetAIParams().groundY;
    const float er = m_Swarm.GetAIParams().enemyRadius;
    const FlowField& flow = m_Swarm.GetFlowField();
    const auto& costs = flow.Costs();
    const Vector3 pp = tf.position;
    const bool evalPath = s_SpotIdx >= 0 && (t - s_SpotStart) >= 9.0f && standing;
    for (int k = 0; k < kBadCount; ++k) s_Now[k] = 0;

    std::fill(s_Dens.begin(), s_Dens.end(), (uint16_t)0);
    for (size_t i = 0; i < s_Enemies.size(); ++i)
    {
        if (s_States[i] == Swarm::kStateDead) continue;
        int gx, gz;
        m_Grid.WorldToCell(s_Enemies[i].position, gx, gz);
        if (gx >= 0 && gz >= 0 && gx < W && gz < D) ++s_Dens[(size_t)gz * W + gx];
    }

    int alive = 0, nk[5] = {};
    for (size_t i = 0; i < s_Enemies.size(); ++i)
    {
        Track& tr = s_Tracks[i];
        if (s_States[i] == Swarm::kStateDead) { tr.count = 0; continue; }
        const Swarm::Enemy& e = s_Enemies[i];
        const uint32_t kind = s_Extras.empty() ? Swarm::kEnemyKindMob : s_Extras[i].kind;
        ++alive;
        if (kind < 5) ++nk[kind];
        if (kind == Swarm::kEnemyKindGhost) { tr.count = 0; continue; }   // 壁を抜ける仕様
        const Vector3 p = e.position;

        // 履歴（瞬間移動 = 転送・湧き直しは切る）
        if (tr.count > 0)
        {
            const int last = (tr.head + kHist - 1) % kHist;
            const float jx = p.x - tr.x[last], jz = p.z - tr.z[last];
            if (jx * jx + jz * jz > 9.0f || tr.kind != kind) tr.count = 0;
        }
        int gx, gz;
        m_Grid.WorldToCell(p, gx, gz);
        const bool inside = gx >= 0 && gz >= 0 && gx < W && gz < D;
        const float costNow = (inside && !costs.empty()) ? costs[(size_t)gz * W + gx] : FLT_MAX;
        tr.x[tr.head] = p.x; tr.z[tr.head] = p.z; tr.yaw[tr.head] = e.yaw; tr.cost[tr.head] = costNow;
        tr.head = (tr.head + 1) % kHist;
        tr.count = (std::min)(tr.count + 1, kHist);
        tr.kind = kind;

        const float ks = (kind == Swarm::kEnemyKindElite) ? bomber.eliteScale
                       : (kind == Swarm::kEnemyKindBoss) ? bomber.bossScale : 1.0f;
        const float rw = (std::min)(er * ks * 1.15f, 0.9f);   // SwarmBodyWallRadius
        const float rv = 0.47f * ks;                          // 見た目の半分の幅（立った姿勢で約 0.94m × 倍率）
        const float foot = p.y - gy;
        const float ground = m_Grid.SampleHeight(p.x, p.z);
        const uint8_t zone = inside ? s_Zone[(size_t)gz * W + gx] : 0;

        auto report = [&](int type, float amount, const char* extra)
            {
                ++s_Now[type];
                s_Max[type] = (std::max)(s_Max[type], amount);
                float& lb = s_LastBad[i * kBadCount + type];
                const bool fresh = t - lb > 10.0f;
                lb = t;
                if (!fresh) return;
                ++s_Episodes[type];
                if (s_Episodes[type] <= 40)
                {
                    snprintf(line, sizeof(line), "soak bad %s #%d slot %zu kind %u amt %.2f pos %.2f,%.2f,%.2f foot %.2f ground %.2f raw %.2f cell %d,%d zone %s spot %s %s",
                        kBadName[type], s_Episodes[type], i, kind, amount, p.x, p.y, p.z, foot, ground, rawH(p.x, p.z),
                        gx, gz, zone == 1 ? "summit" : zone == 2 ? "mine" : "plain", kSpotName[s_Cur.kind], extra);
                    AutoTestLog(line);
                }
                lookAt(type, p);
            };

        if (!inside) { report(kOob, 0.0f, ""); continue; }
        if (!m_Grid.IsWalkable(gx, gz)) { report(kInWall, 0.0f, bigBlocked(gx, gz) ? "big" : "small"); continue; }

        // 塞がったマスへの食い込み（壁用の半径 / 見た目の半径）
        float wpBig = 0.0f, wpSmall = 0.0f, visWall = 0.0f;
        const int reach = (int)std::ceil((std::max)(rw, rv) / GridWorld::kCellSize);
        for (int dz = -reach; dz <= reach; ++dz)
            for (int dx = -reach; dx <= reach; ++dx)
            {
                const int cx = gx + dx, cz = gz + dz;
                if (cx < 0 || cz < 0 || cx >= W || cz >= D || m_Grid.IsWalkable(cx, cz)) continue;
                const Vector3 cc = m_Grid.CellToWorld(cx, cz);
                const float hs = GridWorld::kCellSize * 0.5f;
                const float qx = (std::max)(std::fabs(p.x - cc.x) - hs, 0.0f);
                const float qz = (std::max)(std::fabs(p.z - cc.z) - hs, 0.0f);
                const float dist = std::sqrt(qx * qx + qz * qz);
                if (bigBlocked(cx, cz)) { wpBig = (std::max)(wpBig, rw - dist); visWall = (std::max)(visWall, rv - dist); }
                else wpSmall = (std::max)(wpSmall, rw - dist);
            }
        // 崖（足の高さから 0.8m 以上高い生の高さマスが体の中に入っている深さ）
        const float hRef = (std::max)(rawH(p.x, p.z), foot);
        auto cliffDepth = [&](float r)
            {
                float best = 0.0f;
                for (int k = 0; k < 16; ++k)
                {
                    const float a = (float)k * DirectX::XM_2PI / 16.0f;
                    const float cx = std::cos(a), sz = std::sin(a);
                    for (float s = 0.05f; s <= r + 1e-4f; s += 0.05f)
                        if (rawH(p.x + cx * s, p.z + sz * s) - hRef > 0.8f) { best = (std::max)(best, r - s); break; }
                }
                return best;
            };
        const float cp = cliffDepth(rw);
        // 周り 8 方向（+x から反時計 = +z が 2 番目）: B = 塞がったマス、数字 = 足からの生の高さの差
        char nb[96];
        {
            size_t len = 0;
            nb[0] = 0;
            for (int k = 0; k < 8 && len < sizeof(nb) - 8; ++k)
            {
                const float a = (float)k * DirectX::XM_PIDIV4;
                const float qx = p.x + std::cos(a) * (rw + 0.3f), qz = p.z + std::sin(a) * (rw + 0.3f);
                len += m_Grid.IsWalkableAt(Vector3(qx, 0.0f, qz))
                    ? snprintf(nb + len, sizeof(nb) - len, " %+.1f", rawH(qx, qz) - hRef)
                    : snprintf(nb + len, sizeof(nb) - len, " B");
            }
        }
        char extra[420];
        // MoveCS の判定を CPU で真似る（どの検査が止めているか）。
        // 体の 8 点: B = 塞がったマス / U = 上り / D = 下り（数える時だけ）/ . = 触れていない
        auto moveDiag = [&](char* out, size_t outSize)
            {
                const float step = 1.0f / 60.0f;
                const bool allowDrop = m_Grid.SampleHeight(pp.x, pp.z) <= ground - 1.0f;
                const float rawHere = rawH(p.x, p.z);
                const float footH = allowDrop ? (std::max)(rawHere, foot) : rawHere;
                auto contact = [&](float cx, float cz, char* o8)
                    {
                        const float maxRise = 0.62f * rw + 0.35f;
                        int n = 0;
                        for (int k = 0; k < 8; ++k)
                        {
                            const float a = (float)k * DirectX::XM_PIDIV4;
                            const float qx = cx + std::cos(a) * rw, qz = cz + std::sin(a) * rw;
                            const float rise = rawH(qx, qz) - footH;
                            char ch = '.';
                            if (!m_Grid.IsWalkableAt(Vector3(qx, 0.0f, qz))) ch = 'B';
                            else if (rise > maxRise) ch = 'U';
                            else if (!allowDrop && -rise > maxRise) ch = 'D';
                            o8[k] = ch;
                            if (ch != '.') ++n;
                        }
                        o8[8] = 0;
                        return n;
                    };
                auto stepOk = [&](float tx, float tz, bool bodyCheck, char* why)
                    {
                        if (!m_Grid.IsWalkableAt(Vector3(tx, 0.0f, tz))) { strcpy_s(why, 12, "W"); return false; }
                        const float run = std::hypot(tx - p.x, tz - p.z);
                        const float lim = 0.84f * run + 0.05f;
                        const float riseB = m_Grid.SampleHeight(tx, tz) - ground;
                        const float riseR = std::fabs(rawH(tx, tz) - rawHere);
                        const bool hOk = bodyCheck ? (riseB <= lim && (allowDrop || -riseB <= lim))
                                                   : (riseR <= 0.84f * run + 0.35f);
                        if (!hOk) { strcpy_s(why, 12, "H"); return false; }
                        if (bodyCheck)
                        {
                            char c9[9];
                            if (contact(tx, tz, c9) > 0) { snprintf(why, 12, "%s", c9); return false; }
                        }
                        strcpy_s(why, 12, "ok");
                        return true;
                    };
                char here[9];
                const int nHere = contact(p.x, p.z, here);
                float vx = e.velocity.x, vz = e.velocity.z;
                const bool bodyCheck = (nHere == 0);
                char w0[12], w1[12], w2[12];
                stepOk(p.x + vx * step, p.z + vz * step, bodyCheck, w0);
                stepOk(p.x + vx * step, p.z, bodyCheck, w1);
                stepOk(p.x, p.z + vz * step, bodyCheck, w2);
                const size_t len = strlen(out);
                snprintf(out + len, outSize - len, " move drop %d here %s next %s x %s z %s",
                    allowDrop ? 1 : 0, here, w0, w1, w2);
            };
        if (wpBig > 0.15f || wpSmall > 0.3f)
        {
            snprintf(extra, sizeof(extra), "big %.2f small %.2f r %.2f v %.2f,%.2f,%.2f nb%s", wpBig, wpSmall, rw,
                e.velocity.x, e.velocity.y, e.velocity.z, nb);
            moveDiag(extra, sizeof(extra));
            report(kWallPen, (std::max)(wpBig, wpSmall), extra);
        }
        if (cp > 0.15f)
        {
            snprintf(extra, sizeof(extra), "r %.2f v %.2f,%.2f,%.2f nb%s", rw, e.velocity.x, e.velocity.y, e.velocity.z, nb);
            moveDiag(extra, sizeof(extra));
            report(kCliffPen, cp, extra);
        }
        // 置物（箱・門の柱・外周の岩）への食い込み。格子に載っていない小さい衝突体は GPU の雑魚には見えない
        {
            static std::vector<int> s_Near;
            s_Near.clear();
            m_CollisionSystem.GatherStaticNear(p, rv + 3.0f, s_Near);
            const auto& wcs = m_CollisionSystem.GetWorldColliders();
            const float bodyLo = foot + 0.3f, bodyHi = foot + 1.6f * ks;
            float best = 0.0f;
            Vector3 bestC, bestH;
            for (int idx : s_Near)
            {
                if (idx < 0 || idx >= (int)wcs.size()) continue;
                const auto& wc = wcs[(size_t)idx];
                float depth = -1.0f;
                if (wc.shape == ColliderShape::AABB)
                {
                    if ((std::max)(wc.halfExtents.x, wc.halfExtents.z) > 3.0f) continue;   // 地面・崖の箱は別に測る
                    if (wc.center.y + wc.halfExtents.y < bodyLo || wc.center.y - wc.halfExtents.y > bodyHi) continue;
                    const float qx = (std::max)(std::fabs(p.x - wc.center.x) - wc.halfExtents.x, 0.0f);
                    const float qz = (std::max)(std::fabs(p.z - wc.center.z) - wc.halfExtents.z, 0.0f);
                    depth = rv - std::sqrt(qx * qx + qz * qz);
                }
                else if (wc.shape == ColliderShape::Convex && wc.layer == Layer_Prop)
                {
                    const Vector3 c(p.x, foot + 0.8f, p.z);
                    float sd = -1e9f;
                    for (int k = 0; k < wc.hull.count; ++k) sd = (std::max)(sd, wc.hull.planes[k].SignedDist(c));
                    depth = rv - sd;
                }
                if (depth > best) { best = depth; bestC = wc.center; bestH = wc.halfExtents; }
            }
            if (best > 0.2f)
            {
                snprintf(extra, sizeof(extra), "collider %.1f,%.1f,%.1f half %.2f,%.2f,%.2f rv %.2f", bestC.x, bestC.y, bestC.z,
                    bestH.x, bestH.y, bestH.z, rv);
                report(kPropClip, best, extra);
            }
        }
        if (ks > 1.01f)
        {
            const float vis = (std::max)(visWall, cliffDepth(rv));
            if (vis > 0.3f) { snprintf(extra, sizeof(extra), "%s rv %.2f rw %.2f", kind == Swarm::kEnemyKindBoss ? "boss" : "elite", rv, rw); report(kBigVis, vis, extra); }
        }
        // 地面
        if (ground - foot > 0.3f) { snprintf(extra, sizeof(extra), "vy %.2f", e.velocity.y); report(kSink, ground - foot, extra); }
        // 体の下の一番高い地面（歩けるマスだけ。塞がったマスの高さは支えではない。
        // 双線形の地面は使わない = 塞がったマスの高さが混ざって持ち上がるのを見つけたい）
        float support = -1e9f;
        auto supportAt = [&](float x, float z)
            {
                if (m_Grid.IsWalkableAt(Vector3(x, 0.0f, z))) support = (std::max)(support, rawH(x, z));
            };
        supportAt(p.x, p.z);
        for (int k = 0; k < 8; ++k)
        {
            const float a = (float)k * DirectX::XM_PIDIV4;
            supportAt(p.x + std::cos(a) * rw, p.z + std::sin(a) * rw);
        }
        if (support < -1e8f) support = ground;
        if (foot - support > 0.6f && e.velocity.y >= -0.01f)
        {
            snprintf(extra, sizeof(extra), "support %.2f vy %.2f", support, e.velocity.y);
            report(kHover, foot - support, extra);
        }

        // 経路探索（プレイヤーが立ち止まっている間、8 秒の窓）
        if (evalPath && tr.count == kHist && e.animIndex != 2u)
        {
            const int o = tr.head;   // 一杯の時は一番古い
            float path = 0.0f, turn = 0.0f;
            for (int k = 1; k < kHist; ++k)
            {
                const int a = (o + k - 1) % kHist, b = (o + k) % kHist;
                path += std::hypot(tr.x[b] - tr.x[a], tr.z[b] - tr.z[a]);
                const float dy = tr.yaw[b] - tr.yaw[a];
                turn += std::fabs(std::atan2(std::sin(dy), std::cos(dy)));
            }
            const float net = std::hypot(p.x - tr.x[o], p.z - tr.z[o]);
            const float dP = std::hypot(p.x - pp.x, p.z - pp.z);
            int crowd = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int cx = gx + dx, cz = gz + dz;
                    if (cx >= 0 && cz >= 0 && cx < W && cz < D) crowd += s_Dens[(size_t)cz * W + cx];
                }
            const float c0 = tr.cost[o];
            if (dP > 6.0f + rv && crowd < 8)
            {
                const DirectX::SimpleMath::Vector2 fd = flow.Directions().empty()
                    ? DirectX::SimpleMath::Vector2::Zero : flow.Directions()[(size_t)gz * W + gx];
                snprintf(extra, sizeof(extra), "dP %.1f net %.2f path %.2f turn %.0f cost %.1f->%.1f crowd %d v %.2f,%.2f sp %.2f flow %.2f,%.2f nb%s",
                    dP, net, path, DirectX::XMConvertToDegrees(turn), c0 > 1e30f ? -1.0f : c0, costNow > 1e30f ? -1.0f : costNow,
                    crowd, e.velocity.x, e.velocity.z, e.moveSpeed, fd.x, fd.y, nb);
                moveDiag(extra, sizeof(extra));
                if (costNow > 1e30f) report(kUnreach, dP, extra);
                else if (path < 1.0f) report(kStuck, path, extra);
                else if ((path > 3.5f && net < 1.0f) || (turn > 4.0f * DirectX::XM_PI && net < 2.0f))
                    report(kSpin, DirectX::XMConvertToDegrees(turn), extra);
                else if (c0 < 1e30f && c0 - costNow < 1.0f && costNow > 3.0f) report(kNoProg, c0 - costNow, extra);
            }
        }
    }

    // ---------- プレイヤー ----------
    {
        const auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
        const float pFoot = pp.y - (stats.height * 0.5f + stats.radius);
        const float pg = m_Grid.SampleHeight(pp.x, pp.z);
        int pgx, pgz;
        m_Grid.WorldToCell(pp, pgx, pgz);
        auto preport = [&](int type, float amount)
            {
                ++s_Now[type];
                s_Max[type] = (std::max)(s_Max[type], amount);
                const bool fresh = t - s_PlayerLastBad[type] > 10.0f;
                s_PlayerLastBad[type] = t;
                if (!fresh) return;
                ++s_Episodes[type];
                snprintf(line, sizeof(line), "soak bad %s #%d amt %.2f player %.2f,%.2f,%.2f foot %.2f ground %.2f cell %d,%d spot %s %s",
                    kBadName[type], s_Episodes[type], amount, pp.x, pp.y, pp.z, pFoot, pg, pgx, pgz, kSpotName[s_Cur.kind],
                    standing ? "stand" : "walk");
                AutoTestLog(line);
            };
        if (pg - pFoot > 0.4f) preport(kPlayerSink, pg - pFoot);
        // 外周の岩で塞いだ縁のマスにはプレイヤーは入れる（岩の衝突だけが止める）ので数えない
        const bool border = pgx <= 2 || pgz <= 2 || pgx >= W - 3 || pgz >= D - 3;
        if (!border && pgx >= 0 && pgz >= 0 && pgx < W && pgz < D && !m_Grid.IsWalkable(pgx, pgz) && bigBlocked(pgx, pgz))
            preport(kPlayerWall, 0.0f);
    }

    // ---------- 10 秒毎のまとめ ----------
    if (t >= s_NextSum)
    {
        s_NextSum = t + 10.0f;
        std::string now, ep;
        for (int k = 0; k < kBadCount; ++k)
        {
            char b[48];
            snprintf(b, sizeof(b), " %s %d", kBadName[k], s_Now[k]);
            now += b;
            snprintf(b, sizeof(b), " %d", s_Episodes[k]);
            ep += b;
        }
        snprintf(line, sizeof(line), "soak sum t %.0f run %.0f spot %s %s alive %d (mob %d bomb %d elite %d boss %d ghost %d) now%s | ep%s | fps %.0f",
            t, m_RunTime, kSpotName[s_Cur.kind], standing ? "stand" : "walk", alive, nk[0], nk[1], nk[2], nk[3], nk[4],
            now.c_str(), ep.c_str(), ImGui::GetIO().Framerate);
        AutoTestLog(line);
    }
}

REGISTER_BATTLE_AUTOTEST("soak", AutoTestSoak)
