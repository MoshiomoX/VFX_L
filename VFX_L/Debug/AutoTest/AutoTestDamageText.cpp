// ============================================================
// AutoTestDamageText.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=damagetext
// ダメージの数字（2026-10-17）：正面の的を撃ち、GPU から届いた数字の数・値を記録して撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "UI/DamageNumbers.h"

namespace
{
    class AutoTestDamageText final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・無敵・MP 無限、正面 +Z 8〜10m に的 3 体（HP 300）+ 猛毒の沼一式（毒の数字）、
//   カメラ背後 9m / 26°。2 秒から 0.5 秒毎に `damagetext t total active`、
//   3 / 4.5 / 6 秒 `damagetext look <n>`、7.5 秒 done
// ============================================================
void AutoTestDamageText::Run()
{
    char line[240];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    static float s_NextLog = 2.0f;
    static int s_Look = 0;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player)) { auto& mp = m_Registry.Get<ManaComponent>(m_Player); mp.max = mp.current = 1.0e6f; }
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            FillAllFrames(bp);
            PlacePoisonSet(bp);
            // VFXL_DTX_ITEM=fire / ice / stone / arrow / arc：ホーミングボルトの代わり（属性の色の確認。2026-10-17）
            ItemID extra = ItemID::HomingBolt;
            char itemEnv[32] = {};
            if (GetEnvironmentVariableA("VFXL_DTX_ITEM", itemEnv, sizeof(itemEnv)) > 0)
            {
                if (strcmp(itemEnv, "fire") == 0) extra = ItemID::Fireball;
                else if (strcmp(itemEnv, "ice") == 0) extra = ItemID::IceLance;
                else if (strcmp(itemEnv, "stone") == 0) extra = ItemID::StoneShot;
                else if (strcmp(itemEnv, "arrow") == 0) extra = ItemID::GoldenArrow;
                else if (strcmp(itemEnv, "arc") == 0) extra = ItemID::ArcBolt;
            }
            bool placed = false;
            for (int rot = 0; rot < 4 && !placed; ++rot)
                for (int r = 5; r < 9 && !placed; ++r)
                    for (int c = 5; c < 9 && !placed; ++c)
                        placed = BackpackLogic::Place(bp, extra, r, c, rot) >= 0;
            bp.dirty = true;
        }
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int i = 0; i < 3; ++i)
        {
            const float x = tf.position.x + ((float)i - 1.0f) * 2.2f, z = tf.position.z + 8.0f + (float)(i % 2) * 2.0f;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 300.0f, 0.0f);
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(26.0f);
        cam.distance = 9.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("damagetext start");
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    if (m_AutoTime >= s_NextLog)
    {
        const auto& dn = m_GameUI.Damage();
        snprintf(line, sizeof(line), "damagetext t %.1f total %u active %d elem none %u fire %u ice %u lightning %u arcane %u earth %u poison %u wind %u light %u",
            m_AutoTime, dn.TotalAdded(), dn.ActiveCount(), dn.ElementCount(0), dn.ElementCount(1), dn.ElementCount(2), dn.ElementCount(3),
            dn.ElementCount(4), dn.ElementCount(5), dn.ElementCount(6), dn.ElementCount(7), dn.ElementCount(8));
        AutoTestLog(line);
        s_NextLog += 0.5f;
    }
    const float looks[3] = { 3.0f, 4.5f, 6.0f };
    if (s_Look < 3 && m_AutoTime >= looks[s_Look])
    {
        snprintf(line, sizeof(line), "damagetext look %d", s_Look);
        AutoTestLog(line);
        ++s_Look;
    }
    if (m_AutoStep == 1 && m_AutoTime >= 7.5f) { AutoTestLog("damagetext done"); m_AutoStep = 2; }
}

REGISTER_BATTLE_AUTOTEST("damagetext", AutoTestDamageText)
