// ============================================================
// AutoTestChain.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=chain
// VFXL_BATTLE_AUTOTEST=chain：火球 + 石弾 → 隕石の誘発。隕石が落ちるか、石弾を外すと止まるか
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestChain final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 基本魔法 → 上級魔法の誘発（VFXL_BATTLE_AUTOTEST=chain）
// 1 秒: 湧き停止・全消し・無敵・魔力無限。開始時の 3x3 枠に 隕石（十字）を中央、火球を左上、石弾を左下
//       （どちらの上下左右も隕石の腕に掛かる）。正面 12m に動かない的を 3 体。横から見下ろし
// 1 秒毎に 届いた誘発の数・撃った隕石の数・弾と範囲の数を記録（隕石 1.8 秒毎に 1 個のはず）
// 7 秒: 石弾を外す → 隕石は目覚めていない（増えないはず）。10 秒: 石弾を戻す → 再開。13 秒 done
// 各段の頭で 杖の中身（triggered / triggerMask）と 隕石の説明（DescribePlaced の traits）も記録
// ============================================================
void AutoTestChain::Run()
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
    const int lo = BackpackComponent::GRID / 2 - 1;
    static float s_Next = 2.0f;

    auto utf8 = [](const std::wstring& w)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            std::string o(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
            return o;
        };
    auto logState = [&](const char* tag)
        {
            std::string t = std::string(tag) + " wand:";
            for (const auto& s : wand.spells)
            {
                char b[96];
                snprintf(b, sizeof(b), " [id %d trig %d mask %u cd %.2f]", (int)s.id, s.triggered ? 1 : 0, s.triggerMask, s.castInterval);
                t += b;
            }
            AutoTestLog(t.c_str());
            for (int i = 0; i < (int)bp.items.size(); ++i)
            {
                const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, i);
                std::string l = std::string(tag) + " sheet " + utf8(sh.title) + " :";
                for (const auto& tr : sh.traits) l += " [" + utf8(tr) + "]";
                AutoTestLog(l.c_str());
            }
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (float x : { -2.0f, 0.0f, 2.0f })
            m_Swarm.SpawnEnemy(Vector3(pp.x + x, gy, pp.z + 12.0f), 100000.0f, 0.0f);

        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::Meteor, lo + 1, lo + 1, 0);
        BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
        BackpackLogic::Place(bp, ItemID::StoneShot, lo + 2, lo, 0);
        bp.dirty = true;
        wand.castingPaused = false;

        // 背後から的（+Z 12m）を画面の真ん中に。横からだと窓が画面より大きい時に的が外へ出る
        cam.SetYaw(0.0f);
        cam.distance = 14.0f;
        cam.SetPitch(40.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("chain start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep >= 1 && m_AutoStep <= 3 && m_AutoTime >= s_Next)
    {
        if (s_Next == 2.0f || s_Next == 8.0f || s_Next == 11.0f) logState(m_AutoStep == 2 ? "chain off" : "chain on");
        const auto& c = m_Swarm.GetCounters();
        char line[128];
        snprintf(line, sizeof(line), "chain t %.0f events %u meteors %u proj %u areas %u",
            s_Next, m_WeaponSystem.GetTriggerEventsSeen(), m_WeaponSystem.GetTriggeredCasts(),
            c.aliveProjectiles, c.aliveAreas);
        AutoTestLog(line);
        s_Next += 1.0f;

        if (m_AutoStep == 1 && m_AutoTime >= 7.0f)
        {
            // 石弾（左下）を外す → 隕石は火球だけ = 目覚めない
            for (int i = 0; i < (int)bp.items.size(); ++i)
                if (bp.items[i].id == ItemID::StoneShot) { BackpackLogic::Remove(bp, i); break; }
            bp.dirty = true;
            m_AutoStep = 2;
        }
        else if (m_AutoStep == 2 && m_AutoTime >= 10.0f)
        {
            BackpackLogic::Place(bp, ItemID::StoneShot, lo + 2, lo, 0);
            bp.dirty = true;
            m_AutoStep = 3;
        }
        else if (m_AutoStep == 3 && m_AutoTime >= 13.0f)
        {
            AutoTestLog("chain done");
            // 画面の確認用: 石弾をもう一度外してバックパックと隕石の説明を開く（暗く沈んだ隕石 +「未発動」）。
            // バックパックを開くと gameplay が止まってここへ来なくなるので、これが最後
            int meteorIdx = -1;
            for (int i = (int)bp.items.size() - 1; i >= 0; --i)
                if (bp.items[i].id == ItemID::StoneShot) BackpackLogic::Remove(bp, i);
            for (int i = 0; i < (int)bp.items.size(); ++i)
                if (bp.items[i].id == ItemID::Meteor) meteorIdx = i;
            bp.dirty = true;
            m_GameUI.TestShow(1);
            m_GameUI.TestTooltip(meteorIdx, { m_ScreenW * 0.62f, m_ScreenH * 0.30f });
            AutoTestLog("chain look backpack");
            m_AutoStep = 4;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("chain", AutoTestChain)
