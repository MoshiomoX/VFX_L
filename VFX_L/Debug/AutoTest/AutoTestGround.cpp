// ============================================================
// AutoTestGround.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=ground
// VFXL_BATTLE_AUTOTEST=ground：地面のテクスチャ（2026-10-03）を平原・高い所から・山頂の崖・坂・洞窟の底・洞窟の入口で撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestGround final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 地面のテクスチャ（VFXL_BATTLE_AUTOTEST=ground、2026-10-03）
// 湧き停止・全消し・無敵・詠唱停止。1.2 秒毎にプレイヤーとカメラを置き直して `ground look <名>`:
//   plain（開始地点、普段のカメラ）→ plainNoGrass（同じ所で草を消す = 地面そのもの）→ high（40m / 50° の俯瞰）→
//   summit（山頂の重心から中央へ 32m の平原から山頂の崖を見上げる）→ ramp（山頂の 1 本目の坂の坂下から坂を見る）→
//   mine（洞窟の一番奥、洞の底と壁）→ mouth（洞窟の 1 本目の坂の上から坑を見下ろす）、`ground done`
// 第 2・3 面は VFXL_STAGE=2 / 3 で
// ============================================================
void AutoTestGround::Run()
{
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& cam = m_Camera.Camera();
    const auto& lay = m_TerrainLayout;
    static Vector3 s_Start;
    static int s_Shot = 0;
    static float s_Next = 0.0f;
    static bool s_GrassWas = true;   // 面の設定（遺跡は草無し）。plainNoGrass の後に戻す
    char line[200];

    auto place = [&](const Vector3& p, float yawDeg, float dist, float pitch)
        {
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            cam.SetYaw(yawDeg);
            cam.distance = dist;
            cam.SetPitch(pitch);
            cam.SnapToTarget();
        };
    auto yawTo = [](const Vector3& from, const Vector3& to)
        {
            return DirectX::XMConvertToDegrees(std::atan2(-(to.x - from.x), to.z - from.z));   // forward.x = -sin(yaw)
        };
    auto centroid = [&](const std::vector<int>& cells)
        {
            Vector3 c;
            for (int i : cells) c += m_Grid.CellToWorld(i % m_Grid.Width(), i / m_Grid.Width());
            return cells.empty() ? c : c / (float)cells.size();
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        s_Start = tf.position;
        s_GrassWas = m_Grass.GetSettings().enabled;
        s_Shot = 0;
        s_Next = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1 || m_AutoTime < s_Next) return;

    // 前の撮影を記録してから 1.2 秒後に次へ（外の撮影は記録を見て撮る）
    auto& grass = m_Grass.GetSettings();
    switch (s_Shot)
    {
    case 0:
    {
        grass.enabled = s_GrassWas;
        place(s_Start, 0.0f, 8.0f, 28.0f);
        char tl[96];   // トゥーンのアウトラインが使えるか（2026-10-04）
        snprintf(tl, sizeof(tl), "ground toon outline ready %d enabled %d samples %u",
            (int)m_Outline.IsReady(), (int)m_Outline.GetParams().enabled, Application::Get().GetGraphics().GetSampleCount());
        AutoTestLog(tl);
        AutoTestLog("ground look plain");
        break;
    }
    case 1: grass.enabled = false; AutoTestLog("ground look plainNoGrass"); break;
    case 2: grass.enabled = s_GrassWas; place(s_Start, 30.0f, 40.0f, 50.0f); AutoTestLog("ground look high"); break;
    case 3:
        if (!lay.summitCells.empty())
        {
            const Vector3 sc = centroid(lay.summitCells);
            Vector3 toCenter = s_Start - sc;
            toCenter.y = 0.0f;
            toCenter.Normalize();
            const Vector3 p = sc + toCenter * 32.0f;
            place(p, yawTo(p, sc), 10.0f, 8.0f);
            AutoTestLog("ground look summit");
        }
        break;
    case 4:
        if (!lay.summitRamps.empty())
        {
            const auto& r = lay.summitRamps[0];
            const Vector3 p = r.top + r.down * 22.0f;   // 坂の麓より少し先
            place(p, yawTo(p, r.top), 9.0f, 18.0f);
            AutoTestLog("ground look ramp");
        }
        break;
    case 5:
        if (lay.hasMineDeep)
        {
            const Vector3 mc = centroid(lay.mineCells);
            place(lay.mineDeep, yawTo(lay.mineDeep, mc), 9.0f, 30.0f);
            AutoTestLog("ground look mine");
        }
        break;
    case 6:
        if (!lay.mineRamps.empty())
        {
            const auto& r = lay.mineRamps[0];
            const Vector3 p = r.top - r.down * 3.0f;   // 坂の上（平原側）から坑を見下ろす
            place(p, yawTo(p, r.top + r.down * 10.0f), 7.0f, 35.0f);
            AutoTestLog("ground look mouth");
        }
        break;
    default:
        snprintf(line, sizeof(line), "ground done stage %d", m_StageIndex);
        AutoTestLog(line);
        AutoTestLog("ground done");
        m_AutoStep = 2;
        return;
    }
    ++s_Shot;
    s_Next = m_AutoTime + 1.2f;
}

REGISTER_BATTLE_AUTOTEST("ground", AutoTestGround)
