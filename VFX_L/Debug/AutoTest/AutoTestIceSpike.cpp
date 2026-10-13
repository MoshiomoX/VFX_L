// ============================================================
// AutoTestIceSpike.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=icespike
// VFXL_BATTLE_AUTOTEST=icespike：上級魔法アイススパイク（2026-10-10）。アイスランス + ストーンショットで目覚めるか・
//   足元から狙った敵の奥まで氷のトゲが走るか・線の上の敵がまとめて凍るか・説明カードの文を記録し、トゲを撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestIceSpike final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };

    std::string Utf8(const std::wstring& w)
    {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string o(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
        return o;
    }
}

// ============================================================
// 1 秒: 湧き停止・全消し・無敵・魔力無限。バックパックを全面の枠にして、真ん中にアイススパイク（T 字）、
//   アイスランスとストーンショットはトゲに隣り合う所を探して置く（GetTriggerDrivers に入る所）。
//   正面 +Z 6m / 9m に動かない雑魚 5 体 x 2 列（HP 300 = 死なずに凍るのが見える）。斜め横から
// 0.25 秒毎に `icespike t events casts fx spikes alive frozen` を記録。
// 最初のトゲが出たら +0.08 / +0.35 / +1.0 秒 `icespike look rise / full / hold`、
// 7 秒 反対の横から低く `icespike look side`、11 秒 敵・弾を消して近くで 1 回（CastIceSpike を直に）
//   `icespike look closerise / close / closesink`、done
// ============================================================
void AutoTestIceSpike::Run()
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
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    auto& cam = m_Camera.Camera();
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    char line[256];

    static float s_NextLog = 1.5f, s_FirstFx = -1.0f;
    static int s_Looks = 0;
    static uint32_t s_MaxFrozen = 0;

    auto indexOf = [&](ItemID id) { for (int i = 0; i < (int)bp.items.size(); ++i) if (bp.items[i].id == id) return i; return -1; };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();

        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::IceSpike, 4, 4, 0);
        // 前提の 2 つ：トゲに隣り合う（誘発の前提に数えられる）最初の場所へ
        for (ItemID need : { ItemID::IceLance, ItemID::StoneShot })
        {
            bool placed = false;
            for (int r = 0; r < BackpackComponent::GRID && !placed; ++r)
                for (int c = 0; c < BackpackComponent::GRID && !placed; ++c)
                {
                    const int idx = BackpackLogic::Place(bp, need, r, c, 0);
                    if (idx < 0) continue;
                    const int spike = indexOf(ItemID::IceSpike);
                    const auto drv = BackpackLogic::GetTriggerDrivers(bp, spike);
                    const int now = indexOf(need);
                    if (std::find(drv.begin(), drv.end(), now) != drv.end())
                    {
                        placed = true;
                        snprintf(line, sizeof(line), "icespike place %d at %d,%d", (int)need, r, c);
                        AutoTestLog(line);
                    }
                    else
                        BackpackLogic::Remove(bp, now);
                }
        }
        bp.dirty = true;
        wand.castingPaused = false;
        snprintf(line, sizeof(line), "icespike ready %d", (int)BackpackLogic::IsTriggerReady(bp, indexOf(ItemID::IceSpike)));
        AutoTestLog(line);

        // 説明カード（アイテムそのもの）の文と行
        const ItemInfo::Sheet sh = ItemInfo::Describe(ItemID::IceSpike);
        std::string l = "icespike sheet " + Utf8(sh.category) + " / " + Utf8(sh.title) + " : " + Utf8(sh.description);
        for (const auto& t : sh.traits) l += " [" + Utf8(t) + "]";
        for (const auto& s : sh.stats) l += " {" + Utf8(s.label) + " " + Utf8(s.value) + "}";
        AutoTestLog(l.c_str());

        // 正面 6m に立ち止まった雑魚の列 + その後ろ 9m にもう 1 列（死なない HP、動かない = トゲの列が見える）
        const float gy = m_Swarm.GetAIParams().groundY;
        for (int i = 0; i < 10; ++i)
        {
            const float x = pp.x - 3.0f + (float)(i % 5) * 1.5f, z = pp.z + 6.0f + (float)(i / 5) * 3.0f;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 300.0f, 0.0f);
        }

        // 斜め横から（トゲの列は正面 6〜10m に出る）
        cam.SetYaw(60.0f);
        cam.distance = 11.0f;
        cam.SetPitch(24.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        m_AutoStep = 1;
    }
    if (m_AutoStep < 1) return;

    // ---- 記録 ----
    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog += 0.25f;
        std::vector<Swarm::Enemy> en; std::vector<uint32_t> st; std::vector<Swarm::EnemyExtra> ex; std::vector<Vector4> status;
        int alive = 0, frozen = 0;
        if (m_Swarm.DebugReadEnemies(en, st, &ex, &status) && status.size() == en.size())
            for (size_t i = 0; i < en.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead) continue;
                ++alive;
                if (status[i].z > 0.0f) ++frozen;
            }
        s_MaxFrozen = (std::max)(s_MaxFrozen, (uint32_t)frozen);
        snprintf(line, sizeof(line), "icespike t %.2f events %u casts %u fx %u spikes %d alive %d frozen %d",
            m_AutoTime, m_WeaponSystem.GetTriggerEventsSeen(), m_WeaponSystem.GetTriggeredCasts(),
            m_SpellFx.IceSpikeCasts(), m_SpellFx.IceSpikes().ActiveCount(), alive, frozen);
        AutoTestLog(line);
    }

    // ---- 撮影 ----
    if (s_FirstFx < 0.0f && m_SpellFx.IceSpikeCasts() > 0) s_FirstFx = m_AutoTime;
    if (s_FirstFx >= 0.0f)
    {
        const float since = m_AutoTime - s_FirstFx;
        if (s_Looks == 0 && since >= 0.08f) { AutoTestLog("icespike look rise"); ++s_Looks; }
        else if (s_Looks == 1 && since >= 0.35f) { AutoTestLog("icespike look full"); ++s_Looks; }
        else if (s_Looks == 2 && since >= 1.0f) { AutoTestLog("icespike look hold"); ++s_Looks; }
    }
    if (m_AutoStep == 1 && m_AutoTime >= 7.0f)
    {
        cam.SetYaw(-70.0f);   // 反対の横から低めに（トゲの高さ・地面との合い方）
        cam.distance = 9.0f;
        cam.SetPitch(12.0f);
        cam.SnapToTarget();
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_SpellFx.IceSpikes().ActiveCount() >= 4)
    {
        AutoTestLog("icespike look side");
        m_AutoStep = 3;
    }
    else if (m_AutoStep >= 2 && m_AutoStep <= 3 && m_AutoTime >= 11.0f)
    {
        snprintf(line, sizeof(line), "icespike summary casts %u maxFrozen %u", m_SpellFx.IceSpikeCasts(), s_MaxFrozen);
        AutoTestLog(line);
        // 近くで 1 回だけ（敵・弾を消して、プレイヤーの足元から +Z へ 8m）。斜め横から低く
        m_Swarm.KillAll();
        m_Swarm.ClearProjectiles();
        m_Swarm.ClearAreas();
        wand.castingPaused = true;
        m_SpellFx.Clear();
        cam.SetYaw(70.0f);
        cam.distance = 8.0f;
        cam.SetPitch(16.0f);
        cam.SnapToTarget();
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 11.6f)
    {
        m_SpellFx.CastIceSpike(pp, pp + Vector3(0.0f, 0.0f, 8.0f), 1.0f, [this](float x, float z) { return m_Grid.SampleHeight(x, z); });
        s_FirstFx = m_AutoTime;
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5 && m_AutoTime >= s_FirstFx + 0.07f) { AutoTestLog("icespike look closerise"); m_AutoStep = 6; }
    else if (m_AutoStep == 6 && m_AutoTime >= s_FirstFx + 0.4f) { AutoTestLog("icespike look close"); m_AutoStep = 7; }
    else if (m_AutoStep == 7 && m_AutoTime >= s_FirstFx + 1.15f) { AutoTestLog("icespike look closesink"); m_AutoStep = 8; }
    else if (m_AutoStep == 8 && m_AutoTime >= s_FirstFx + 1.8f)
    {
        AutoTestLog("icespike done");
        m_AutoStep = 9;
    }
}

REGISTER_BATTLE_AUTOTEST("icespike", AutoTestIceSpike)
