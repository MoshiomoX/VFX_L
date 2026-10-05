// ============================================================
// AutoTestBomber.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=bomber
// VFXL_BATTLE_AUTOTEST=bomber：詠唱を止め、湧きを止めてプレイヤーの近くに自爆兵を出し、
// GPU の counter（活き数・撃破・プレイヤーへの累計ダメージ・範囲数）と HP が変わる度に記録する
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBomber final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        uint32_t m_AutoLast[4] = {};   // aliveEnemies / killCount / playerDamage / aliveAreas
        float    m_AutoLastHp = -1.0f;
    };
}

// 1 秒: 詠唱停止・湧き停止・GPU の雑魚を全部消して自爆兵 3 体（寄って来て点火 → 爆発するはず）
// 9 秒: プレイヤーに付いて動く小さな毒の輪（半径 1.5m、0.4 秒毎に 10）+ 自爆兵 3 体。
//       触れて点火した後、導火線（1 秒）の途中で 2 回目の tick に倒されるはず
//       （撃破数が増え、プレイヤーへの累計ダメージは増えない）。
//       同時にバックパックへ火球を置いて詠唱を戻す（MP が減って回復するかを毎秒の行で見る）
void AutoTestBomber::Run()
{
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;   // 画面の連写にデバッグの線を入れない
        m_Camera.Camera().SetPitch(50.0f);   // 足元（丸い影・警告の輪）が映るように見下ろす
        m_Camera.Camera().distance = 10.0f;
        m_Mobs.QueueDebugBombers(3);
        AutoTestLog("bomber A: casting paused, 3 bombers");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 9.0f)
    {
        Swarm::Area ring;
        ring.center = m_Registry.Get<TransformComponent>(m_Player).position;
        ring.radius = 1.5f;
        ring.damage = 10.0f;
        ring.tickInterval = 0.4f;
        ring.timeLeft = 8.0f;
        ring.flags = Swarm::kAreaFollowPlayer;
        m_Swarm.SpawnArea(ring);
        m_Mobs.QueueDebugBombers(3);
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int mid = BackpackComponent::GRID / 2;
            BackpackLogic::Place(bp, ItemID::Fireball, mid, mid, 0);
            bp.dirty = true;
        }
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        AutoTestLog("bomber B: damage ring r1.5 10/0.4s, 3 bombers");
        m_AutoStep = 2;
    }

    // 1 秒毎に平均 fps と MP（今 / 上限 / 予約中）
    static int s_Frames = 0;
    static float s_FpsTimer = 0.0f;
    ++s_Frames;
    s_FpsTimer += ImGui::GetIO().DeltaTime;
    if (s_FpsTimer >= 1.0f)
    {
        char fps[96];
        const ManaComponent* mp = m_Registry.Has<ManaComponent>(m_Player) ? &m_Registry.Get<ManaComponent>(m_Player) : nullptr;
        snprintf(fps, sizeof(fps), "fps %.1f mp %.1f/%.0f pending %.1f", s_Frames / s_FpsTimer,
            mp ? mp->current : -1.0f, mp ? mp->max : -1.0f, mp ? mp->pendingSpend : -1.0f);
        AutoTestLog(fps);
        s_Frames = 0;
        s_FpsTimer = 0.0f;
    }

    // counter（リードバック）と HP が変わった時だけ 1 行
    const auto& c = m_Swarm.GetCounters();
    const uint32_t now[4] = { c.aliveEnemies, c.killCount, c.playerDamage, c.aliveAreas };
    const float hp = m_Registry.Has<HealthComponent>(m_Player) ? m_Registry.Get<HealthComponent>(m_Player).current : 0.0f;
    if (std::equal(now, now + 4, m_AutoLast) && hp == m_AutoLastHp) return;
    std::copy(now, now + 4, m_AutoLast);
    m_AutoLastHp = hp;

    char line[160];
    snprintf(line, sizeof(line), "alive %u kills %u dmgTotal %.2f areas %u hp %.1f",
        now[0], now[1], now[2] / 100.0f, now[3], hp);
    AutoTestLog(line);
}

REGISTER_BATTLE_AUTOTEST("bomber", AutoTestBomber)
