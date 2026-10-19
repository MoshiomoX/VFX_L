// ============================================================
// AutoTestSteps.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=steps（2026-10-19、段々の台地）
//   A: 記録の坂（stepRamps）の低い端の手前にプレイヤーを置き、横から見る（"steps look ramp"）→ 高い端へ 4 秒歩く
//      （0.25 秒毎に "steps walk t x z y"。終わりに登れたか = 高さが 1 段分上がったか）→ "steps look top"
//   B: 0 段と 1 段の境（坂ではない崖）の手前 2.5m に置き、崖へ 2 秒歩く（止まるはず）→ "steps look cliff"
//      → 歩きながら 1 回ジャンプ（2.5m の段には 1 回のジャンプでぎりぎり乗れる）→ 1.5 秒後に上に乗れたか
//   "steps result rampClimbed <0/1> cliffBlocked <0/1> jumpedUp <0/1>" → done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestSteps final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);

        Vector3 m_Dir;          // 歩く向き（水平）
        Vector3 m_Start;        // 歩き始めの位置
        float m_Phase = 0.0f;
        float m_NextLog = 0.0f;
        bool  m_RampClimbed = false, m_CliffBlocked = false, m_JumpedUp = false;
        float m_CliffBaseY = 0.0f;
        bool  m_Jumped = false;
    };
}

void AutoTestSteps::Run(float dt)
{
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const float kCs = GridWorld::kCellSize;
    const float stepH = m_TerrainMap.stepH;

    auto place = [&](float x, float z)
        {
            tf.position = Vector3(x, m_Grid.SampleHeight(x, z) + 1.0f, z);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        };
    auto lookFromSide = [&](const Vector3& dir, float pitch)
        {
            // 進む向きに直角な横から（FollowCamera: forward = (-sin yaw, 0, cos yaw)）
            const Vector3 side(-dir.z, 0.0f, dir.x);
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-side.x, side.z)));
            cam.SetPitch(pitch);
            cam.distance = 14.0f;
            cam.avoidOcclusion = false;
            cam.SnapToTarget();
        };
    auto logPos = [&](const char* tag)
        {
            char line[160];
            snprintf(line, sizeof(line), "steps %s t %.2f x %.1f z %.1f y %.2f ground %.2f", tag, m_Phase,
                tf.position.x, tf.position.z, tf.position.y, m_Grid.SampleHeight(tf.position.x, tf.position.z));
            AutoTestLog(line);
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

        // 坂：0 段から 1 段へ上る物を 1 本選ぶ
        const MapData::StepRamp* ramp = nullptr;
        for (const auto& r : m_TerrainMap.stepRamps) if (r.lo == 0) { ramp = &r; break; }
        if (!ramp || stepH <= 0.0f)
        {
            AutoTestLog("steps no ramp");
            AutoTestLog("steps done");
            m_AutoStep = 99;
            return;
        }
        // 下る向き（side）：低い端はその先
        const int dx = (ramp->side == 0) ? 1 : (ramp->side == 1) ? -1 : 0;
        const int dz = (ramp->side == 2) ? 1 : (ramp->side == 3) ? -1 : 0;
        const Vector3 c = Vector3(m_Grid.OriginX() + (ramp->x + ramp->w * 0.5f) * kCs, 0.0f,
            m_Grid.OriginZ() + (ramp->z + ramp->d * 0.5f) * kCs);
        const float halfLen = ((dx != 0) ? ramp->w : ramp->d) * kCs * 0.5f;
        const Vector3 down((float)dx, 0.0f, (float)dz);
        const Vector3 lowEnd = c + down * halfLen;
        m_Dir = -down;   // 低い端 → 高い端
        const Vector3 start = lowEnd + down * 3.0f;
        place(start.x, start.z);
        m_Start = tf.position;
        lookFromSide(m_Dir, 18.0f);
        char line[160];
        snprintf(line, sizeof(line), "steps ramp at %d,%d %dx%d side %d lo %d (of %d ramps, step %.2f)",
            ramp->x, ramp->z, ramp->w, ramp->d, ramp->side, ramp->lo, (int)m_TerrainMap.stepRamps.size(), stepH);
        AutoTestLog(line);
        m_AutoStep = 1;
        m_Phase = 0.0f;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.5f) { AutoTestLog("steps look ramp"); m_AutoStep = 2; m_Phase = 0.0f; m_NextLog = 0.0f; }
    else if (m_AutoStep == 2)
    {
        // 高い端へ歩く（最長 4 秒：坂 8m + 手前 3m = 11m、5.5 m/s）。
        // 登れた = 坂の先の 1 段のマス（坂ではない）に接地した。先の台地が狭いと向こうへ落ちるので、登れた所で止める
        m_Phase += dt;
        if (m_Phase >= m_NextLog) { logPos("walk"); m_NextLog += 0.25f; }
        {
            int gx, gz;
            m_Grid.WorldToCell(tf.position, gx, gz);
            const size_t c = (size_t)gz * m_TerrainMap.gw + gx;
            const float ground = m_Grid.SampleHeight(tf.position.x, tf.position.z);
            if (gx >= 0 && gz >= 0 && gx < m_TerrainMap.gw && gz < m_TerrainMap.gd
                && m_TerrainMap.stepLevel[c] == 1 && m_TerrainMap.stepRampAt[c] < 0 && tf.position.y - ground < 1.3f)
                m_RampClimbed = true;
        }
        if (m_Phase >= 4.0f || m_RampClimbed)
        {
            pcs.testInput = false;
            char line[96];
            snprintf(line, sizeof(line), "steps ramp climbed %d (dy %.2f, %.2f s)", m_RampClimbed ? 1 : 0, tf.position.y - m_Start.y, m_Phase);
            AutoTestLog(line);
            AutoTestLog("steps look top");
            m_AutoStep = 3;
            m_Phase = 0.0f;
        }
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 8.0f)
    {
        // 崖：0 段のマスの隣が 1 段（坂でも水たまりでもない）な所。中央から近い順
        const int gw = m_TerrainMap.gw, gd = m_TerrainMap.gd;
        int bestX = -1, bestZ = -1, bestDx = 0, bestDz = 0;
        float bestD = 1.0e9f;
        for (int z = 2; z < gd - 2; ++z)
            for (int x = 2; x < gw - 2; ++x)
            {
                const size_t c = (size_t)z * gw + x;
                if (m_TerrainMap.stepLevel[c] != 0 || m_TerrainMap.stepRampAt[c] >= 0 || !m_Grid.IsWalkable(x, z)) continue;
                const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (const auto& n : nb)
                {
                    const size_t cn = (size_t)(z + n[1]) * gw + (x + n[0]);
                    if (m_TerrainMap.stepLevel[cn] != 1 || m_TerrainMap.stepRampAt[cn] >= 0 || !m_Grid.IsWalkable(x + n[0], z + n[1])) continue;
                    // 手前 1 マスも 0 段（助走）
                    const size_t cb = (size_t)(z - n[1]) * gw + (x - n[0]);
                    if (m_TerrainMap.stepLevel[cb] != 0 || m_TerrainMap.stepRampAt[cb] >= 0) continue;
                    const float d = (float)((x - gw / 2) * (x - gw / 2) + (z - gd / 2) * (z - gd / 2));
                    if (d < bestD) { bestD = d; bestX = x; bestZ = z; bestDx = n[0]; bestDz = n[1]; }
                }
            }
        if (bestX < 0)
        {
            AutoTestLog("steps no cliff");
            m_AutoStep = 7;
            return;
        }
        m_Dir = Vector3((float)bestDx, 0.0f, (float)bestDz);
        const Vector3 cell = m_Grid.CellToWorld(bestX, bestZ);
        const Vector3 start = cell - m_Dir * 1.5f;   // 崖の面（マスの境）まで 2.5m
        place(start.x, start.z);
        m_Start = tf.position;
        m_CliffBaseY = tf.position.y;
        lookFromSide(m_Dir, 18.0f);
        char line[128];
        snprintf(line, sizeof(line), "steps cliff at %d,%d toward %d,%d", bestX, bestZ, bestDx, bestDz);
        AutoTestLog(line);
        m_AutoStep = 4;
        m_Phase = 0.0f;
        m_NextLog = 0.0f;
    }
    else if (m_AutoStep == 4)
    {
        // 崖へ 2 秒歩く：面の手前で止まるはず（登らない）
        m_Phase += dt;
        if (m_Phase >= m_NextLog) { logPos("push"); m_NextLog += 0.25f; }
        if (m_Phase >= 2.0f)
        {
            const float moved = (Vector3(tf.position.x, 0.0f, tf.position.z) - Vector3(m_Start.x, 0.0f, m_Start.z)).Length();
            m_CliffBlocked = moved < 3.0f && tf.position.y - m_CliffBaseY < stepH * 0.5f;
            char line[128];
            snprintf(line, sizeof(line), "steps cliff blocked %d (moved %.2f dy %.2f)", m_CliffBlocked ? 1 : 0, moved, tf.position.y - m_CliffBaseY);
            AutoTestLog(line);
            AutoTestLog("steps look cliff");
            m_AutoStep = 5;
            m_Phase = 0.0f;
            m_Jumped = false;
        }
    }
    else if (m_AutoStep == 5 && m_AutoTime >= 13.5f)
    {
        m_AutoStep = 6;
        m_Phase = 0.0f;
        m_NextLog = 0.0f;
    }
    else if (m_AutoStep == 6)
    {
        // 歩きながら 1 回ジャンプ → 上に乗れたか
        m_Phase += dt;
        if (m_Phase >= m_NextLog) { logPos("jump"); m_NextLog += 0.25f; }
        if (m_Phase >= 2.0f)
        {
            m_JumpedUp = tf.position.y - m_CliffBaseY >= stepH * 0.8f;
            char line[128];
            snprintf(line, sizeof(line), "steps jumped up %d (dy %.2f)", m_JumpedUp ? 1 : 0, tf.position.y - m_CliffBaseY);
            AutoTestLog(line);
            AutoTestLog("steps look jumped");
            m_AutoStep = 7;
            m_Phase = 0.0f;
        }
    }
    else if (m_AutoStep == 7 && m_Phase < 1.0f)
    {
        m_Phase += dt;
        if (m_Phase >= 1.0f)
        {
            char line[128];
            snprintf(line, sizeof(line), "steps result rampClimbed %d cliffBlocked %d jumpedUp %d",
                m_RampClimbed ? 1 : 0, m_CliffBlocked ? 1 : 0, m_JumpedUp ? 1 : 0);
            AutoTestLog(line);
            AutoTestLog("steps done");
            m_AutoStep = 99;
        }
    }

    // ---- 入力の代わり：歩く段だけ ----
    const bool walking = (m_AutoStep == 2 || m_AutoStep == 4 || m_AutoStep == 6);
    if (!walking) { pcs.testInput = false; return; }
    Vector3 camF = cam.GetForward(); camF.y = 0; camF.Normalize();
    Vector3 camR = cam.GetRight();   camR.y = 0; camR.Normalize();
    pcs.testInput = true;
    pcs.testMove = Vector2(m_Dir.Dot(camR), m_Dir.Dot(camF));
    pcs.testJump = false;
    pcs.testSlide = false;
    if (m_AutoStep == 6 && !m_Jumped && m_Phase >= 0.2f) { pcs.testJump = true; m_Jumped = true; }
}

REGISTER_BATTLE_AUTOTEST("steps", AutoTestSteps)
