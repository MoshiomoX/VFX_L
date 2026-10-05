// ============================================================
// AutoTestBoss.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=boss
// VFXL_BATTLE_AUTOTEST=boss：無敵・湧き停止、門の前へ移って F（Boss の HP は流れを見るため 300）、
// 毎秒 Boss の状態を記録。倒せば「ステージクリア」→ リザルト
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBoss final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 門 → Boss → クリア（VFXL_BATTLE_AUTOTEST=boss）
// 1 秒: 無敵・湧き停止・全消し、門の手前 2m へ移る。2 秒: F を押した扱い（Boss HP 300）。
// 毎秒 "boss t ..." 行。出来事は "boss event"。倒した後はシーンが「ステージクリア」→ リザルトへ
// ============================================================
void AutoTestBoss::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;   // 三択で止めない
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Entity portal = m_Stage.GetPortal();
        if (m_Registry.IsValid(portal) && m_Registry.Has<InteractableComponent>(portal))
        {
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            const Vector3 pp = m_Registry.Get<InteractableComponent>(portal).basePos;
            Vector3 dir = tf.position - pp; dir.y = 0.0f;
            if (dir.LengthSquared() < 1e-4f) dir = Vector3(1, 0, 0);
            dir.Normalize();
            tf.position = Vector3(pp.x + dir.x * 2.0f, pp.y + 1.0f, pp.z + dir.z * 2.0f);
            if (m_Registry.Has<RigidbodyComponent>(m_Player))
                m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            m_Camera.Camera().SnapToTarget();
            AutoTestLog("boss at portal");
        }
        else
            AutoTestLog("boss: no portal");
        m_Stage.bossHp = 300.0f;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        m_AutoInteract = true;
        AutoTestLog("boss press F");
        m_AutoStep = 2;
    }

    if (const char* ev = m_Stage.ConsumeEvent())
    {
        char line[96];
        snprintf(line, sizeof(line), "boss event %s", ev);
        AutoTestLog(line);
    }

    // 倒した 2 秒後（「ステージクリア」の幕が出ている）に 1 行。外から撮る
    static float s_ClearedAt = -1.0f;
    if (m_AutoStep == 2 && m_Stage.IsCleared()) { s_ClearedAt = m_AutoTime; m_AutoStep = 3; }
    if (m_AutoStep == 3 && m_AutoTime >= s_ClearedAt + 2.0f) { AutoTestLog("boss done"); m_AutoStep = 4; }

    static float s_Log = 0.0f;
    s_Log += ImGui::GetIO().DeltaTime;
    if (m_AutoStep >= 2 && s_Log >= 1.0f)
    {
        s_Log = 0.0f;
        const auto& info = m_Swarm.GetBossInfo();
        char line[160];
        snprintf(line, sizeof(line), "boss t %.0f alive %u hp %.0f / %.0f ratio %.2f cleared %d",
            m_AutoTime, info.alive, Swarm::HpFromFixed(info.hp > info.maxHp ? 0u : info.hp),
            Swarm::HpFromFixed(info.maxHp), m_Stage.BossHpRatio(), m_Stage.IsCleared() ? 1 : 0);
        AutoTestLog(line);
    }
}

REGISTER_BATTLE_AUTOTEST("boss", AutoTestBoss)
