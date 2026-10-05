// ============================================================
// AutoTestPortal.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=portal
// VFXL_BATTLE_AUTOTEST=portal：Boss の門（石の拱 + 粒子の渦、2026-10-03）を正面・斜めから撮り、
// 門の前で F → 渦が止まるか
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPortal final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: Boss の門（VFXL_BATTLE_AUTOTEST=portal、2026-10-03）
// 1 秒: 湧き停止・全消し・無敵、門の位置・向き・渦の中心を記録し、門の正面 8m に立つ（カメラは門を向く）。
// 3 秒 `portal look front`、斜め 50 度へ回して 5 秒 `portal look side`、
// 門の前 2m へ動いて 6 秒に F（m_AutoInteract）、8 秒 `portal look used`（渦が止まって消えた所）、9 秒 done
// ============================================================
void AutoTestPortal::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& cam = m_Camera.Camera();
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    char line[200];
    const Vector3 gate = m_Stage.GetPortalCenter();
    const float yaw = DirectX::XMConvertToRadians(m_Stage.GetPortalYaw());
    const Vector3 faceDir(std::sin(yaw), 0.0f, std::cos(yaw));   // 門の面が向く方
    auto standAt = [&](float dist, float sideDeg, float camDist, float pitch)
        {
            const float a = DirectX::XMConvertToRadians(sideDeg);
            const Vector3 d(faceDir.x * std::cos(a) - faceDir.z * std::sin(a), 0.0f, faceDir.x * std::sin(a) + faceDir.z * std::cos(a));
            const Vector3 p = gate + d * dist;
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(d.x, -d.z)));   // 門の方（-d）を見る
            cam.distance = camDist;
            cam.SetPitch(pitch);
            cam.SnapToTarget();
        };
    auto stamp = [&](const char* what)
        {
            snprintf(line, sizeof(line), "portal look %s vfx %u interactable %d fps %.0f", what, m_PortalVfx,
                (m_Registry.IsValid(m_Stage.GetPortal()) && m_Registry.Has<InteractableComponent>(m_Stage.GetPortal())) ? 1 : 0,
                ImGui::GetIO().Framerate);
            AutoTestLog(line);
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        snprintf(line, sizeof(line), "portal at %.1f,%.1f,%.1f yaw %.0f vfx %u",
            gate.x, gate.y, gate.z, m_Stage.GetPortalYaw(), m_PortalVfx);
        AutoTestLog(line);
        standAt(8.0f, 0.0f, 7.0f, 12.0f);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 3.0f) { stamp("front"); m_AutoStep = 2; }
    else if (m_AutoStep == 2 && m_AutoTime >= 3.6f) { standAt(8.0f, 50.0f, 7.0f, 12.0f); m_AutoStep = 3; }
    else if (m_AutoStep == 3 && m_AutoTime >= 5.0f) { stamp("side"); m_AutoStep = 4; }
    else if (m_AutoStep == 4 && m_AutoTime >= 5.6f) { standAt(2.0f, 0.0f, 7.0f, 12.0f); m_AutoStep = 5; }
    else if (m_AutoStep == 5 && m_AutoTime >= 6.0f) { m_AutoInteract = true; m_AutoStep = 6; }
    else if (m_AutoStep == 6 && m_AutoTime >= 6.3f) { standAt(8.0f, 0.0f, 7.0f, 12.0f); m_AutoStep = 7; }
    else if (m_AutoStep == 7 && m_AutoTime >= 8.0f) { stamp("used"); m_AutoStep = 8; }
    else if (m_AutoStep == 8 && m_AutoTime >= 9.0f) { AutoTestLog("portal done"); m_AutoStep = 9; }
}

REGISTER_BATTLE_AUTOTEST("portal", AutoTestPortal)
