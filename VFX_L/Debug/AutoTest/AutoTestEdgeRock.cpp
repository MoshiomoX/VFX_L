// ============================================================
// AutoTestEdgeRock.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=edgerock
// VFXL_BATTLE_AUTOTEST=edgerock：外周の一番手前の岩（2026-10-03、少し外へ下げて入り込む物だけ衝突）。
// 四辺の 8 か所でプレイヤーを壁へ押し、止まった所の縁からの距離を記録・横から撮る。最後に縁沿いの雑魚
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestEdgeRock final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 外周の岩の衝突（VFXL_BATTLE_AUTOTEST=edgerock、2026-10-03）
// 1 秒: 湧き停止・全消し・無敵・詠唱停止。四辺 × 2 か所（辺に沿って -35m / +25m）を順に:
//   縁の 4m 内に立ち、カメラは壁沿いに横から（6m / 12°）、1.2 秒壁へ押す → `edgerock <n> stop <縁からの m>`
//   （正 = 縁より内で止まった = 入り込んだ岩に当たった、0 付近 = 外周の崖の箱）と `edgerock look <n>`。
// 最後に北の縁の 3〜8m 内に雑魚 30 体（プレイヤーは 12m 内）、6 秒後に塞いだマスの中に居る数 `edgerock mobs`
// ============================================================
void AutoTestEdgeRock::Run(float dt)
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    char line[200];
    static int s_Sample = 0;
    static float s_T = 0.0f;
    const float edge = m_Grid.WorldWidth() * 0.5f - GridWorld::kCellSize;   // フィールドの縁（崖のマスの内側）
    struct Sample { Vector3 n, t; float along; };
    static const Sample kSamples[] = {
        { { 0, 0, 1 }, { 1, 0, 0 }, -35.0f }, { { 0, 0, 1 }, { 1, 0, 0 }, 25.0f },
        { { 0, 0, -1 }, { 1, 0, 0 }, -35.0f }, { { 0, 0, -1 }, { 1, 0, 0 }, 25.0f },
        { { 1, 0, 0 }, { 0, 0, 1 }, -35.0f }, { { 1, 0, 0 }, { 0, 0, 1 }, 25.0f },
        { { -1, 0, 0 }, { 0, 0, 1 }, -35.0f }, { { -1, 0, 0 }, { 0, 0, 1 }, 25.0f },
    };
    constexpr int kSampleCount = (int)std::size(kSamples);

    if (m_AutoStep == 0)
    {
        if (m_AutoTime < 1.0f) return;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        cam.avoidOcclusion = false;
        s_Sample = 0;
        s_T = 0.0f;
        // 外周の岩の衝突（縁の 8m 以内にある凸体）の数・静的な衝突の総数
        int edgeHulls = 0, statics = 0;
        m_Registry.CreateView<TransformComponent, ColliderComponent>()
            .Each([&](Entity, TransformComponent& t, ColliderComponent& c)
                {
                    ++statics;
                    const float d = edge - (std::max)(std::fabs(t.position.x), std::fabs(t.position.z));
                    if (c.shape == ColliderShape::Convex && d < 8.0f) ++edgeHulls;
                });
        snprintf(line, sizeof(line), "edgerock colliders: edge rock hulls %d / all colliders %d", edgeHulls, statics);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1)
    {
        const Sample& sm = kSamples[s_Sample];
        if (s_T == 0.0f)
        {
            const Vector3 p = sm.n * (edge - 4.0f) + sm.t * sm.along;
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
            rb.velocity = Vector3::Zero;
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-sm.t.x, sm.t.z)));   // 壁沿いに見る
            cam.distance = 6.0f;
            cam.SetPitch(12.0f);
            cam.SnapToTarget();
        }
        s_T += dt;
        Vector3 camF = cam.GetForward(); camF.y = 0; camF.Normalize();
        Vector3 camR = cam.GetRight();   camR.y = 0; camR.Normalize();
        const bool push = s_T > 0.2f && s_T < 1.4f;
        pcs.testInput = true;
        pcs.testMove = push ? Vector2(sm.n.Dot(camR), sm.n.Dot(camF)) : Vector2::Zero;
        pcs.testSlide = false;
        pcs.testJump = false;
        if (s_T >= 1.6f && s_T - dt < 1.6f)
        {
            const float stop = edge - tf.position.Dot(sm.n);
            snprintf(line, sizeof(line), "edgerock %d stop %.2f (n %.0f,%.0f along %.0f, y %.1f)",
                s_Sample, stop, sm.n.x, sm.n.z, sm.along, tf.position.y);
            AutoTestLog(line);
            snprintf(line, sizeof(line), "edgerock look %d", s_Sample);
            AutoTestLog(line);
        }
        if (s_T >= 2.2f)
        {
            s_T = 0.0f;
            if (++s_Sample >= kSampleCount)
            {
                pcs.testInput = false;
                m_AutoStep = 2;
            }
        }
    }
    else if (m_AutoStep == 2)
    {
        // 北の縁沿いに雑魚 30 体、プレイヤーは縁の 12m 内
        const Vector3 pp = Vector3(0.0f, 0.0f, edge - 12.0f);
        tf.position = Vector3(pp.x, m_Grid.SampleHeight(pp.x, pp.z) + 1.0f, pp.z);
        rb.velocity = Vector3::Zero;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (int i = 0; i < 30; ++i)
        {
            const float x = -45.0f + 3.0f * (float)i;
            const float z = edge - 3.0f - (float)(i % 6);
            int gx, gz;
            m_Grid.WorldToCell({ x, 0.0f, z }, gx, gz);
            if (!m_Grid.IsWalkable(gx, gz)) continue;
            m_Swarm.SpawnEnemy({ x, m_Grid.SampleHeight(x, z) + gy, z }, 100000.0f, 3.5f, Swarm::kEnemyKindMob);
        }
        s_T = 0.0f;
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3)
    {
        s_T += dt;
        if (s_T >= 6.0f)
        {
            std::vector<Swarm::Enemy> enemies;
            std::vector<uint32_t> states;
            int alive = 0, inWall = 0, nearEdge = 0;
            if (m_Swarm.DebugReadEnemies(enemies, states))
                for (size_t i = 0; i < enemies.size(); ++i)
                {
                    if (states[i] == Swarm::kStateDead) continue;
                    ++alive;
                    const Vector3 p = enemies[i].position;
                    int gx, gz;
                    m_Grid.WorldToCell(p, gx, gz);
                    if (!m_Grid.IsWalkable(gx, gz)) ++inWall;
                    if (edge - p.z < 2.0f) ++nearEdge;
                }
            snprintf(line, sizeof(line), "edgerock mobs alive %d inBlockedCell %d within2mOfEdge %d", alive, inWall, nearEdge);
            AutoTestLog(line);
            AutoTestLog("edgerock done");
            m_AutoStep = 4;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("edgerock", AutoTestEdgeRock)
