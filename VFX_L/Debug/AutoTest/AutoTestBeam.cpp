// ============================================================
// AutoTestBeam.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=beam
// VFXL_BATTLE_AUTOTEST=beam：ホーミングボルト + アークボルト → 魔導光線の連鎖。正面の的へ向けて光線が出るか、
// 拡大鏡を隣に置くと射程と太さが伸びるか（2026-10-13）。横から撮る
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
        void DumpWand(const char* tag);
        void DumpEnemies(const char* tag);
        void SetCamera(bool close);
    };
}

// ============================================================
// TEMP-TEST: 魔導光線（VFXL_BATTLE_AUTOTEST=beam）
// 1 秒: 湧き停止・全消し・無敵・魔力無限。9×9 を枠で埋め、光線（横 3）を (6,4)〜(6,6)、その上にホーミングボルト（横 2）、
//       右下にアークボルト（Z 字）= shapes 自動テストと同じ置き方（基本魔法が多マスになった後の形）。
//       正面 12m に動かない的を 1 体（中心線）、その後ろ 16 / 20 / 21.5m に HP 40（弾は手前の的で消えるので光線しか届かない）。
// A 段（拡大鏡なし）: 光線 2 本。1 本目は横 30m から全体（射程 18m）、2 本目は横 8m の近景（太さ 1.2m）。
//       20 / 21.5m の雑魚は射程外なので生き残る
// B 段（拡大鏡を (7,3) に置く：斜めの (6,4) が光線の左端）: 同じく 2 本。射程 22.5m・太さ 1.5m。
//       20 / 21.5m の雑魚が倒れる（kills で確認）
// 0.5 秒毎に "beam t ..."、各光線の出た 0.6 秒後に "beam look <A|B><n>" + その光線の射程 / 半径。
// 最後にバックパックを開いて光線の説明カード（射程 22.5 (18)）を出して done
// ============================================================
void AutoTestBeam::SetCamera(bool close)
{
    auto& cam = m_Camera.Camera();
    cam.SetYaw(-90.0f);   // 右から（光線が画面を横切る）
    cam.distance = close ? 8.0f : 30.0f;
    cam.SetPitch(close ? 12.0f : 25.0f);
    cam.avoidOcclusion = false;
    cam.SnapToTarget();
}

void AutoTestBeam::DumpWand(const char* tag)
{
    const auto& wand = m_Registry.Get<WandComponent>(m_Player);
    std::string t = std::string("beam wand ") + tag + ":";
    for (const auto& s : wand.spells)
    {
        char b[96];
        snprintf(b, sizeof(b), " [spell id %d trig %d mask %u]", (int)s.id, s.triggered ? 1 : 0, s.triggerMask);
        t += b;
    }
    for (const auto& a : wand.areas)
    {
        char b[128];
        snprintf(b, sizeof(b), " [area id %d trig %d profile %d r %.2f len %.2f mp %.1f]",
            (int)a.id, a.triggered ? 1 : 0, a.profile, a.radius, a.length, a.manaCost);
        t += b;
    }
    AutoTestLog(t.c_str());
}

// 生きている雑魚を全部（的の HP 10 万は除く）: プレイヤーからの前方距離と HP。光線が何 m 先まで当たったかを見る
void AutoTestBeam::DumpEnemies(const char* tag)
{
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    if (!m_Swarm.DebugReadEnemies(en, st)) { AutoTestLog("beam enemies read failed"); return; }
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    std::string t = std::string("beam enemies ") + tag + ":";
    for (size_t i = 0; i < st.size() && i < en.size(); ++i)
    {
        if (st[i] == Swarm::kStateDead) continue;
        const float hp = Swarm::HpFromFixed(en[i].hp);
        if (hp > 1000.0f) continue;
        char b[64];
        snprintf(b, sizeof(b), " [z %.1f x %.1f hp %.0f]", en[i].position.z - pp.z, en[i].position.x - pp.x, hp);
        t += b;
    }
    AutoTestLog(t.c_str());
}

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
    static float s_NextLog = 0.0f;
    static float s_LookAt = -1.0f;
    static int s_Looks = 0;         // この段で撮った数
    static bool s_WasActive = false;
    static bool s_Dumped = false;
    static float s_PhaseStart = 0.0f;
    static uint32_t s_KillsAtPhase = 0;
    static int s_BeamIdx = -1;

    auto spawnBehind = [&]()
    {
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float z : { 16.0f, 20.0f, 21.5f })
            m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + z), 40.0f, 0.0f);
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 12.0f), 100000.0f, 0.0f);   // 的は中心線に 1 体だけ（横に並べると弾がどれに当たるかで光線の向きが 7 度ずれ、後ろの雑魚を外す）
        spawnBehind();

        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        ClearBackpackItems(bp);
        s_BeamIdx = BackpackLogic::Place(bp, ItemID::Beam, 6, 5, 0);   // (6,4)(6,5)(6,6)
        const int homing = BackpackLogic::Place(bp, ItemID::HomingBolt, 5, 4, 0);   // (5,4)(5,5)：真下が光線
        const int arc = BackpackLogic::Place(bp, ItemID::ArcBolt, 7, 6, 0);         // (7,6)〜(8,8)：(7,6) の上 = 光線 (6,6)
        char line[160];
        snprintf(line, sizeof(line), "beam place beam %d homing %d arc %d trigger %d",
            s_BeamIdx, homing, arc, s_BeamIdx >= 0 ? (int)BackpackLogic::IsTriggerReady(bp, s_BeamIdx) : -1);
        AutoTestLog(line);
        bp.dirty = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;

        SetCamera(false);
        AutoTestLog("beam start phase A (no magnifier)");
        s_PhaseStart = m_AutoTime;
        s_KillsAtPhase = m_Swarm.GetCounters().killCount;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 || m_AutoStep == 3)
    {
        const char phase = (m_AutoStep == 1) ? 'A' : 'B';
        const bool active = m_WeaponSystem.GetActiveBeamCount() > 0;
        if (active && !s_WasActive)
        {
            s_LookAt = m_AutoTime + 0.6f;
            SetCamera(s_Looks == 1);   // 2 本目は近景（太さを見る）
        }
        s_WasActive = active;

        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog = m_AutoTime + 0.5f;
            const auto& c = m_Swarm.GetCounters();
            char line[200];
            snprintf(line, sizeof(line), "beam %c t %.1f events %u casts %u beams %d proj %u areas %u kills %u",
                phase, m_AutoTime, m_WeaponSystem.GetTriggerEventsSeen(), m_WeaponSystem.GetTriggeredCasts(),
                m_WeaponSystem.GetActiveBeamCount(), c.aliveProjectiles, c.aliveAreas, c.killCount);
            AutoTestLog(line);
        }
        if (!s_Dumped && m_AutoTime >= s_PhaseStart + 1.0f)
        {
            s_Dumped = true;
            DumpWand(phase == 'A' ? "A" : "B");
        }
        if (s_LookAt > 0.0f && m_AutoTime >= s_LookAt)
        {
            s_LookAt = -1.0f;
            float len = 0.0f, rad = 0.0f;
            m_WeaponSystem.GetBeamDebugSize(0, len, rad);
            char line[128];
            snprintf(line, sizeof(line), "beam size %c%d length %.2f radius %.2f (width %.2f)", phase, s_Looks, len, rad, rad * 2.0f);
            AutoTestLog(line);
            snprintf(line, sizeof(line), "beam look %c%d", phase, s_Looks);
            AutoTestLog(line);
            DumpEnemies(line + 5);   // "A0" など
            if (++s_Looks >= 2)
            {
                m_AutoStep = (phase == 'A') ? 2 : 4;
                s_PhaseStart = m_AutoTime + 1.5f;   // 光線が消えて kills が確定するまで待つ
            }
        }
        if (m_AutoTime >= s_PhaseStart + 20.0f) { AutoTestLog("beam done (timeout)"); m_AutoStep = 5; }
    }
    else if (m_AutoStep == 2 && m_AutoTime >= s_PhaseStart)
    {
        // A 段の結果：16m は倒れ、20 / 21.5m は射程外で生き残るはず
        char line[160];
        snprintf(line, sizeof(line), "beam A result kills %u (16m should die, 20 / 21.5m should live)",
            m_Swarm.GetCounters().killCount - s_KillsAtPhase);
        AutoTestLog(line);
        DumpEnemies("A end");

        // 拡大鏡を (7,3) に：斜めの (6,4) が光線の左端。ホーミング / アークには掛からない
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        const int mag = BackpackLogic::Place(bp, ItemID::Magnifier, 7, 3, 0);
        for (int i = 0; i < (int)bp.items.size(); ++i) if (bp.items[i].id == ItemID::Beam) s_BeamIdx = i;
        const auto infl = BackpackLogic::GetInfluencers(bp, s_BeamIdx);
        snprintf(line, sizeof(line), "beam magnifier placed index %d influencers on beam %d", mag, (int)infl.size());
        AutoTestLog(line);
        bp.dirty = true;
        // 全部置き直す（A 段の生き残りと重ならないように。KillAll は撃破に数えない）
        m_Swarm.KillAll();
        {
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            const float gy = m_Swarm.GetAIParams().groundY;
            m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 12.0f), 100000.0f, 0.0f);
        }
        spawnBehind();
        s_Looks = 0;
        s_Dumped = false;
        s_WasActive = false;
        s_KillsAtPhase = m_Swarm.GetCounters().killCount;
        SetCamera(false);
        AutoTestLog("beam start phase B (magnifier)");
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= s_PhaseStart)
    {
        char line[160];
        snprintf(line, sizeof(line), "beam B result kills %u (16 / 20 / 21.5m should all die)",
            m_Swarm.GetCounters().killCount - s_KillsAtPhase);
        AutoTestLog(line);
        DumpEnemies("B end");

        // バックパックを開いて光線の説明（射程 22.5 (18)・太さ）を出す。以後 gameplay は止まる
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        {
            const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, s_BeamIdx);
            std::string l = "beam sheet :";
            for (const auto& tr : sh.traits) { l += " ["; for (wchar_t w : tr) l += (w < 128) ? (char)w : '?'; l += "]"; }
            for (const auto& st : sh.stats)
            {
                l += " {";
                for (wchar_t w : st.label) l += (w < 128) ? (char)w : '?';
                l += " ";
                for (wchar_t w : st.value) l += (w < 128) ? (char)w : '?';
                l += "}";
            }
            AutoTestLog(l.c_str());
        }
        m_GameUI.TestShow(1);
        m_GameUI.TestTooltip(s_BeamIdx, { m_ScreenW * 0.62f, m_ScreenH * 0.30f });
        AutoTestLog("beam look backpack");
        AutoTestLog("beam done");
        m_AutoStep = 5;
    }
}

REGISTER_BATTLE_AUTOTEST("beam", AutoTestBeam)
