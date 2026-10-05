// ============================================================
// AutoTestArrow.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=arrow
// VFXL_BATTLE_AUTOTEST=arrow：黄金の矢だけをバックパックに置き、正面の的へ撃たせて横から連写する
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestArrow final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 黄金の矢（VFXL_BATTLE_AUTOTEST=arrow）
// 1 秒: 湧き停止・全消し・無敵・MP 無限、正面 14m に動かない的 3 体、バックパックを黄金の矢だけにする。
//       カメラは 14m・見下ろし 15 度で、プレイヤーの右 90 度から見る（矢が画面を横切る。VFXL_ARROW_FAR で 30m・40 度）
// 3 秒から 0.06 秒毎に "arrow look" を 16 回（外から連写）→ "arrow done"
// ============================================================
void AutoTestArrow::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& cam = m_Camera.Camera();
    static int s_Shots = 0;
    static float s_Next = 3.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        // 的はプレイヤーの +Z 側 14m（カメラの yaw 0 = +Z）
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -1.5f, 0.0f, 1.5f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 14.0f), 100000.0f, 0.0f);
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::GoldenArrow, lo + 1, lo + 1, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        cam.SetYaw(-90.0f);   // 右から（矢は画面を左 → 右へ横切る向き）
        // 弾道全体（プレイヤー → 14m 先の的）が画面に入る距離。近すぎると弾と一緒に飛ぶ矢が一瞬しか映らない
        cam.distance = 14.0f;
        cam.SetPitch(15.0f);
        if (GetEnvironmentVariableA("VFXL_ARROW_FAR", nullptr, 0) > 0) { cam.distance = 30.0f; cam.SetPitch(40.0f); }
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("arrow start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= s_Next)
    {
        const auto& c = m_Swarm.GetCounters();
        char line[96];
        snprintf(line, sizeof(line), "arrow look %d shots %u areas %u", s_Shots, c.aliveProjectiles, c.aliveAreas);
        AutoTestLog(line);
        s_Next += 0.06f;
        if (++s_Shots >= 16) { AutoTestLog("arrow done"); m_AutoStep = 2; }
    }
}

REGISTER_BATTLE_AUTOTEST("arrow", AutoTestArrow)
