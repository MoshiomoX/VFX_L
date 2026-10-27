// ============================================================
// AutoTestWater.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=water（VFXL_STAGE=2 で砂漠 = 雨の代わりに砂嵐の濁り）
// 水たまりの水面（World/WaterSurface、2026-10-27）：何も無い時は静か、原因がある時だけ動くか。
//   1 秒: 湧き停止・全消し・無敵・詠唱停止・天候の出来事を止めて昼に固定。一番近い「周り 8 マスも水」の水たまりのマスを探し、
//         その 6m 手前の乾いた地面に立ち、水の方を向く（カメラ 9m / 30 度）。
//   A 静か     : 1.5 秒後 `water look calm`（普段の風だけ）
//   B 水へ歩く : 水の方へ 2.4 秒歩く（足の水しぶき・航跡）→ `water look wade`
//   C 止まる   : 2 秒止まって `water look still`（波紋が静まる）
//   D 跳ぶ     : その場で跳んで着地 0.15 秒後 `water look land`
//   E 爆発     : 水の真ん中に爆発の範囲（kAreaShake）→ 0.4 / 1.2 秒後 `water look blast1 / blast2`
//   F 天候     : 雨（砂漠は砂嵐）を 0.5 秒で始める → 3 秒後 `water look weather`
//   G 渦       : 天候を止め、水の真ん中に竜巻の風源 3 秒 → 1.5 秒後 `water look vortex`
//   H 本番     : カメラを既定（14m / 45 度）に戻して `water look game`
//   毎 0.5 秒 `water t <秒> step <段> inWater <0/1> maxAbs <m> moving <texel> rain <0〜1>`（DebugReadRipples）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestWater final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Place(float x, float z);
        void Face(const Vector3& from, const Vector3& to, float distance, float pitch);
        Vector3 m_Pond;          // 水たまりの真ん中のマスの中心（水面の高さ）
        Vector3 m_Stand;         // 立つ所
        float m_PhaseAt = 0.0f;  // 今の段に入った時刻
        float m_NextLog = 0.0f;
        bool  m_WadeShot = false;
        bool  m_RainStopped = false;
        bool  m_Landed = false;
        bool  m_WasAir = false;
    };
}

void AutoTestWater::Place(float x, float z)
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    tf.position = Vector3(x, m_Grid.SampleHeight(x, z) + 1.0f, z);
    if (m_Registry.Has<RigidbodyComponent>(m_Player))
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
}

// from から to の方を見る（FollowCamera: forward = (-sin yaw, 0, cos yaw)）
void AutoTestWater::Face(const Vector3& from, const Vector3& to, float distance, float pitch)
{
    auto& cam = m_Camera.Camera();
    const Vector3 d = to - from;
    cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-d.x, d.z)));
    cam.SetPitch(pitch);
    cam.distance = distance;
    cam.avoidOcclusion = false;
    cam.SnapToTarget();
}

void AutoTestWater::Run()
{
    auto& pcs = m_PlayerControlSystem;
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    char line[200];

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
        m_Weather.fadeIn = 0.5f;
        m_Weather.fadeOut = 0.5f;

        // 周り 5×5 マスに水が一番多い水たまりのマス（同じなら近い方）。周り 8 マスは全部水のこと
        const float kCs = GridWorld::kCellSize;
        const Vector3 p = tf.position;
        float best = 1.0e9f;
        bool found = false;
        int cells = 0;
        for (int gz = 1; gz < m_Grid.Depth() - 1; ++gz)
            for (int gx = 1; gx < m_Grid.Width() - 1; ++gx)
            {
                const float x = m_Grid.OriginX() + (gx + 0.5f) * kCs, z = m_Grid.OriginZ() + (gz + 0.5f) * kCs;
                float wy;
                if (!m_Water.WaterAt(x, z, wy)) continue;
                ++cells;
                int around = 0;
                bool inner = true;
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        float tmp;
                        const bool w = m_Water.WaterAt(x + dx * kCs, z + dz * kCs, tmp);
                        around += w ? 1 : 0;
                        if (!w && std::abs(dx) <= 1 && std::abs(dz) <= 1) inner = false;
                    }
                if (!inner) continue;
                const float d = (Vector3(x, 0.0f, z) - Vector3(p.x, 0.0f, p.z)).Length();
                const float score = -10.0f * around + 0.05f * d;
                if (score < best) { best = score; m_Pond = Vector3(x, wy, z); found = true; }
            }
        snprintf(line, sizeof(line), "water pond cells %d found %d at (%.1f, %.1f) y %.2f score %.1f", cells, found ? 1 : 0, m_Pond.x, m_Pond.z, m_Pond.y, best);
        AutoTestLog(line);
        if (!found) { AutoTestLog("water no pond"); AutoTestLog("water done"); m_AutoStep = 99; return; }

        // 水の 6m 手前の乾いた歩ける地面（8 方向で一番低い高低差の所）
        float bestScore = 1.0e9f;
        for (int k = 0; k < 16; ++k)
        {
            const float a = k * 6.2831853f / 16.0f;
            for (float r = 5.0f; r <= 8.0f; r += 1.0f)
            {
                const float x = m_Pond.x + std::cos(a) * r, z = m_Pond.z + std::sin(a) * r;
                float wy;
                if (m_Water.WaterAt(x, z, wy)) continue;
                if (!m_Grid.IsWalkableAt(Vector3(x, 0.0f, z))) continue;
                const float score = std::abs(m_Grid.SampleHeight(x, z) - m_Pond.y) + 0.1f * r;
                if (score < bestScore) { bestScore = score; m_Stand = Vector3(x, 0.0f, z); }
                break;
            }
        }
        Place(m_Stand.x, m_Stand.z);
        Face(m_Stand, m_Pond, 9.0f, 30.0f);
        m_GameUI.hideAll = true;   // 足元が HUD の技のアイコンに隠れるので（本番のカメラの `game` だけ出す）
        snprintf(line, sizeof(line), "water stand (%.1f, %.1f) ground %.2f", m_Stand.x, m_Stand.z, m_Grid.SampleHeight(m_Stand.x, m_Stand.z));
        AutoTestLog(line);
        m_PhaseAt = m_AutoTime;
        m_NextLog = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0 || m_AutoStep >= 99) return;

    const float t = m_AutoTime - m_PhaseAt;
    auto next = [&](int step) { m_AutoStep = step; m_PhaseAt = m_AutoTime; };

    // 毎 0.5 秒：波紋の大きさ
    if (m_AutoTime >= m_NextLog)
    {
        m_NextLog = m_AutoTime + 0.5f;
        float maxAbs = 0.0f;
        uint32_t moving = 0;
        m_Water.DebugReadRipples(maxAbs, moving);
        float wy = 0.0f;
        const float feetY = tf.position.y - 1.3f;
        const bool inWater = m_Water.WaterAt(tf.position.x, tf.position.z, wy) && feetY < wy;
        snprintf(line, sizeof(line), "water t %.2f step %d inWater %d maxAbs %.4f moving %u rain %.2f wind %.2f",
            m_AutoTime, m_AutoStep, inWater ? 1 : 0, maxAbs, moving, m_Weather.RainIntensity(), m_Weather.Wind().strength);
        AutoTestLog(line);
    }

    switch (m_AutoStep)
    {
    case 1:   // A 静か
        if (t >= 1.5f) { AutoTestLog("water look calm"); next(2); }
        break;
    case 2:   // B 水へ歩く（水たまりの真ん中に着いたら止まる）
    {
        const Vector3 toPond(m_Pond.x - tf.position.x, 0.0f, m_Pond.z - tf.position.z);
        const Vector3 fromStand(m_Pond.x - m_Stand.x, 0.0f, m_Pond.z - m_Stand.z);
        const bool arrived = toPond.Length() < 0.6f || toPond.Dot(fromStand) <= 0.0f;
        pcs.testMove = arrived ? Vector2::Zero : Vector2(0.0f, 0.6f);   // ゆっくり（足の水しぶきと航跡を見る）
        // 水に入って真ん中の 1.5m 手前（まだ歩いている）で 1 枚
        if (!m_WadeShot && (toPond.Length() < 1.5f || arrived)) { AutoTestLog("water look wade"); m_WadeShot = true; }
        if ((arrived && m_WadeShot) || t >= 8.0f) next(3);
        break;
    }
    case 3:   // C 止まる（波紋が静まる）
        pcs.testMove = Vector2::Zero;
        if (t >= 2.0f) { AutoTestLog("water look still"); next(4); }
        break;
    case 4:   // D 跳ぶ → 着地
    {
        const bool grounded = m_Registry.Has<RigidbodyComponent>(m_Player) && m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        pcs.testJump = !m_WasAir && t < 0.5f;   // 一度だけ（地面を離れたら押すのを止める）
        if (!grounded) m_WasAir = true;
        if (m_WasAir && grounded && !m_Landed) { m_Landed = true; m_PhaseAt = m_AutoTime; }
        if (m_Landed && m_AutoTime - m_PhaseAt >= 0.15f) { AutoTestLog("water look land"); pcs.testJump = false; next(5); }
        if (t >= 3.0f && !m_Landed) { AutoTestLog("water land timeout"); pcs.testJump = false; next(5); }
        break;
    }
    case 5:   // E 爆発（水の真ん中）
        if (t >= 1.5f)
        {
            // プレイヤーの 3m 先（水の上なら）。無ければ水たまりの真ん中
            Vector3 dir(m_Pond.x - m_Stand.x, 0.0f, m_Pond.z - m_Stand.z);
            dir.Normalize();
            const Vector3 ahead = tf.position + dir * 3.0f;
            float wy;
            if (m_Water.WaterAt(ahead.x, ahead.z, wy)) m_Pond = Vector3(ahead.x, wy, ahead.z);
            Swarm::Area blast;
            blast.center = m_Pond;
            blast.radius = 3.0f;
            blast.damage = 0.0f;
            blast.tickInterval = 0.0f;
            blast.timeLeft = 0.4f;
            blast.flags = Swarm::kAreaShake;
            m_Swarm.SpawnArea(blast);
            AutoTestLog("water blast spawned");
            next(6);
        }
        break;
    case 6:
        if (t >= 0.4f) { AutoTestLog("water look blast1"); next(7); }
        break;
    case 7:
        if (t >= 0.8f) { AutoTestLog("water look blast2"); next(8); }
        break;
    case 8:   // F 天候（草原 = 雨、砂漠 = 砂嵐）
        if (t >= 1.0f)
        {
            m_Weather.TriggerNow(30.0f);
            snprintf(line, sizeof(line), "water weather kind %d", (int)m_Weather.CurrentKind());
            AutoTestLog(line);
            next(9);
        }
        break;
    case 9:
        if (t >= 3.0f) { AutoTestLog("water look weather"); next(10); }
        break;
    case 10:  // G 渦（天候を止めてから）
        if (!m_RainStopped) { m_Weather.StopNow(); m_RainStopped = true; }
        if (t >= 1.5f)
        {
            SwarmSystem::WindBurst b;
            b.pos = m_Pond;
            b.radius = 6.0f;
            b.swirl = 14.0f;
            b.updraft = 3.0f;
            b.inflow = 2.0f;
            b.life = 3.0f;
            m_Swarm.AddWindBurst(b);
            AutoTestLog("water vortex");
            next(11);
        }
        break;
    case 11:
        if (t >= 1.5f) { AutoTestLog("water look vortex"); next(12); }
        break;
    case 12:  // H 本番のカメラ
        if (t >= 2.0f)
        {
            auto& cam = m_Camera.Camera();
            cam.SetPitch(45.0f);
            cam.distance = 14.0f;
            cam.SnapToTarget();
            m_GameUI.hideAll = false;
            next(13);
        }
        break;
    case 13:
        if (t >= 0.8f)
        {
            AutoTestLog("water look game");
            AutoTestLog(("  prof " + FrameProfiler::Get().Summary()).c_str());   // 「Water」の GPU 時間
            next(14);
        }
        break;
    case 14:
        if (t >= 0.5f) { AutoTestLog("water done"); m_AutoStep = 99; }
        break;
    }
}

REGISTER_BATTLE_AUTOTEST("water", AutoTestWater)
