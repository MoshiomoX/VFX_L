// ============================================================
// AutoTestEdge.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=edge
// VFXL_BATTLE_AUTOTEST=edge：外周の岩山を、縁の近くの水平視点・高い所からの俯瞰・フィールドの中央から撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestEdge final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 外周の岩山（VFXL_BATTLE_AUTOTEST=edge）
// 1 秒: 湧き停止・全消し・無敵、北の縁から 15m の所へ移り縁を向く（カメラ 10m・見下ろし 8 度）→ 3 秒 "edge look near"
// → カメラ 60m・見下ろし 45 度 → 5.5 秒 "edge look high" → フィールドの中央・既定のカメラ → 8 秒 "edge look center"（fps も）→ 9 秒 done
// ============================================================
void AutoTestEdge::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    auto place = [&](float x, float z)
        {
            tf.position = Vector3(x, m_Grid.SampleHeight(x, z) + 1.0f, z);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        place(0.0f, m_Grid.WorldDepth() * 0.5f - 15.0f);
        cam.SetYaw(0.0f);   // yaw 0 = +Z（北の縁）を向く（FollowCamera: forward = (-sin, ., cos)）
        cam.distance = 10.0f;
        cam.SetPitch(8.0f);
        cam.avoidOcclusion = false;   // 後ろの台地で寄らないように
        cam.SnapToTarget();
        m_AutoStep = 1;
    }
    // 撮影は記録の後に外から非同期で行うので、記録してから 0.6 秒はカメラを動かさない
    else if (m_AutoStep == 1 && m_AutoTime >= 3.0f) { AutoTestLog("edge look near"); m_AutoStep = 11; }
    else if (m_AutoStep == 11 && m_AutoTime >= 3.6f)
    {
        cam.distance = 60.0f;
        cam.SetPitch(45.0f);
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 5.5f) { AutoTestLog("edge look high"); m_AutoStep = 12; }
    else if (m_AutoStep == 12 && m_AutoTime >= 6.1f)
    {
        cam.avoidOcclusion = true;
        const FollowCamera def;
        cam.distance = def.distance;
        cam.ResetView();
        cam.SetYaw(0.0f);
        place(0.0f, 0.0f);
        cam.SnapToTarget();
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 8.0f)
    {
        char line[96];
        snprintf(line, sizeof(line), "edge look center fps %.0f", ImGui::GetIO().Framerate);
        AutoTestLog(line);
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 9.0f)
    {
        AutoTestLog("edge done");
        m_AutoStep = 5;
    }
}

REGISTER_BATTLE_AUTOTEST("edge", AutoTestEdge)
