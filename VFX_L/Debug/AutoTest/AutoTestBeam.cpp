// ============================================================
// AutoTestBeam.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=beam
// VFXL_BATTLE_AUTOTEST=beam：追尾弾 + 弧 → 魔導光線の誘発。正面の的へ向けて光線が出るか（横から撮る）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestBeam final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 魔導光線（VFXL_BATTLE_AUTOTEST=beam）
// 1 秒: 湧き停止・全消し・無敵・魔力無限。3x3 枠の中段に光線（横 3）、上段中央に追尾弾、下段中央に弧
//       （どちらの上下左右も光線の中心に掛かる）。正面 12m に動かない的を 3 体。右横から（光線が画面を横切る）
// 0.5 秒毎に 届いた誘発・撃った回数・出ている光線・範囲・雑魚の数を記録。
// 光線が出た瞬間から 0.6 秒後（溜めが終わって光線が伸びた所）に "beam look <n>"（外から撮る）。3 本撮ったら done
// ============================================================
void AutoTestBeam::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& cam = m_Camera.Camera();
    static float s_NextLog = 0.0f;
    static float s_LookAt = -1.0f;
    static int s_Looks = 0;
    static bool s_WasActive = false;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -1.5f, 0.0f, 1.5f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 12.0f), 100000.0f, 0.0f);
        // 的の後ろ（16 / 20m）に HP 40 を 2 体：弾は手前の的で消えるので届かない。光線が貫通していれば倒れる（kills で確認）
        m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 16.0f), 40.0f, 0.0f);
        m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 20.0f), 40.0f, 0.0f);

        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        const int lo = BackpackComponent::GRID / 2 - 1;
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Beam, lo, lo + 1, 0);            // 上段（横 3 マス）
        BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo + 2, 0);  // 右中（十字の上が光線の右端）
        BackpackLogic::Place(bp, ItemID::ArcBolt, lo + 1, lo, 0);         // 左中（十字の上が光線の左端）
        bp.dirty = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;

        cam.SetYaw(-90.0f);   // 右から
        cam.distance = 16.0f;
        cam.SetPitch(18.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("beam start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1)
    {
        const bool active = m_WeaponSystem.GetActiveBeamCount() > 0;
        if (active && !s_WasActive) s_LookAt = m_AutoTime + 0.6f;
        s_WasActive = active;

        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog = m_AutoTime + 0.5f;
            const auto& c = m_Swarm.GetCounters();
            char line[160];
            snprintf(line, sizeof(line), "beam t %.1f events %u casts %u beams %d proj %u areas %u kills %u",
                m_AutoTime, m_WeaponSystem.GetTriggerEventsSeen(), m_WeaponSystem.GetTriggeredCasts(),
                m_WeaponSystem.GetActiveBeamCount(), c.aliveProjectiles, c.aliveAreas, c.killCount);
            AutoTestLog(line);
        }
        static bool s_Dumped = false;
        if (!s_Dumped && m_AutoTime >= 2.0f)
        {
            // 杖の中身（誘発の bit が立っているか）
            s_Dumped = true;
            const auto& wand = m_Registry.Get<WandComponent>(m_Player);
            std::string t = "beam wand:";
            for (const auto& s : wand.spells)
            {
                char b[96];
                snprintf(b, sizeof(b), " [spell id %d trig %d mask %u]", (int)s.id, s.triggered ? 1 : 0, s.triggerMask);
                t += b;
            }
            for (const auto& a : wand.areas)
            {
                char b[96];
                snprintf(b, sizeof(b), " [area id %d trig %d profile %d r %.2f]", (int)a.id, a.triggered ? 1 : 0, a.profile, a.radius);
                t += b;
            }
            AutoTestLog(t.c_str());
        }
        if (s_LookAt > 0.0f && m_AutoTime >= s_LookAt)
        {
            s_LookAt = -1.0f;
            char line[64];
            snprintf(line, sizeof(line), "beam look %d", s_Looks);
            AutoTestLog(line);
            if (++s_Looks >= 3)
            {
                // 最後にバックパックを開いて光線の説明（発動中 / 太さ / 射程）を出す。以後 gameplay は止まる
                auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
                int beamIdx = -1;
                for (int i = 0; i < (int)bp.items.size(); ++i)
                    if (bp.items[i].id == ItemID::Beam) beamIdx = i;
                {
                    const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, beamIdx);
                    std::string l = "beam sheet :";
                    for (const auto& tr : sh.traits) { l += " ["; for (wchar_t w : tr) l += (w < 128) ? (char)w : '?'; l += "]"; }
                    AutoTestLog(l.c_str());
                }
                m_GameUI.TestShow(1);
                m_GameUI.TestTooltip(beamIdx, { m_ScreenW * 0.62f, m_ScreenH * 0.30f });
                AutoTestLog("beam look backpack");
                AutoTestLog("beam done");
                m_AutoStep = 2;
            }
        }
        if (m_AutoTime >= 25.0f) { AutoTestLog("beam done (timeout)"); m_AutoStep = 2; }
    }
}

REGISTER_BATTLE_AUTOTEST("beam", AutoTestBeam)
