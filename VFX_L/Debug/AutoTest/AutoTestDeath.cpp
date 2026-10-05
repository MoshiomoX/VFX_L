// ============================================================
// AutoTestDeath.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=death
// VFXL_BATTLE_AUTOTEST=death：死んだ敵の砕け散り（2026-10-02）。正面の近くに一撃で死ぬ雑魚を出し続け、
// 追尾弾と火球で倒す。0.12 秒毎に `death look <n>`（外から撮る）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestDeath final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 死んだ敵の砕け散り（VFXL_BATTLE_AUTOTEST=death、2026-10-02）
// 1 秒: 湧き停止・全消し・無敵・MP 無限・入力 0、バックパックは追尾弾 + 火球（3x3 の中央と左上）。
//   カメラはプレイヤーの後ろ 9m・俯角 34°。0.6 秒毎に正面 6〜8m へ HP 8 の雑魚を 3 体（追尾弾 1 発で死ぬ）。
// 2 秒から 0.12 秒毎に `death look <n> kills <k>`（外から撮る、30 枚）、6 秒 done
// ============================================================
void AutoTestDeath::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;
    static float s_NextWave = 0.0f;
    static int s_Looks = 0;
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    const float gy = m_Swarm.GetAIParams().groundY;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo + 1, 0);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.distance = 9.0f;
        cam.SetPitch(34.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("death start: waves of 3 mobs (hp 8) at +Z 6-8m every 0.6s, homing + fireball");
        s_NextWave = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    if (m_AutoTime >= s_NextWave && m_AutoTime < 5.5f)
    {
        s_NextWave = m_AutoTime + 0.6f;
        for (int k = 0; k < 3; ++k)
            m_Swarm.SpawnEnemy(Vector3(pp.x + (float)(k - 1) * 1.6f, gy, pp.z + 6.0f + (float)(rand() % 3)), 8.0f, 0.5f);
    }
    if (s_Looks < 30 && m_AutoTime >= 2.0f + 0.12f * (float)s_Looks)
    {
        char line[64];
        snprintf(line, sizeof(line), "death look %d kills %u", s_Looks, m_Swarm.GetCounters().killCount);
        ++s_Looks;
        AutoTestLog(line);
    }
    if (m_AutoTime >= 6.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("death done");
        m_AutoStep = 2;
    }
}

REGISTER_BATTLE_AUTOTEST("death", AutoTestDeath)
