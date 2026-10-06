// ============================================================
// AutoTestMagnifier.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=magnifier
// VFXL_BATTLE_AUTOTEST=magnifier：拡大鏡の斜めに火球と隕石、効かない所にもう 1 つ火球を置いて撃たせ、
// 集約後の半径・消費とアイテム説明を記録する（大きさの見比べは外から連写）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestMagnifier final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 拡大鏡（VFXL_BATTLE_AUTOTEST=magnifier）
// 1 秒: 湧きを止めて正面 8〜9m に動かない的（HP 1000）を 3 体。
//       開始時の 3x3 枠の左上に火球、右下に隕石（ここまでは素の大きさ）。魔力無限・経験値 0・見下ろし
// 9 秒: 中央に拡大鏡 → 斜めの 2 つが 1.5 倍になるはず（弾・爆発・隕石の警告の輪）
// 各段の 1 秒後に 集約後の魔法（半径・消費）と アイテム説明（DescribePlaced）を autotest.log へ。
// 1 秒毎に 弾・範囲の数。画面の見比べは外から連写（"magnifier A" / "magnifier B" の後）
// ============================================================
void AutoTestMagnifier::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    const int lo = BackpackComponent::GRID / 2 - 1;

    auto logState = [&](const char* tag)
    {
        char line[240];
        for (const SpellStats& s : wand.spells)
        {
            const ItemCommon* ic = ItemDatabase::GetCommon(s.id);
            snprintf(line, sizeof(line), "%s spell %s radius %.3f mana %.2f damage %.1f",
                tag, ic ? ic->name : "?", s.radius, s.manaCost, s.damage);
            AutoTestLog(line);
        }
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, i);
            auto utf8 = [](const std::wstring& w)
            {
                const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
                std::string o(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
                return o;
            };
            std::string t = std::string(tag) + " sheet " + utf8(sh.title) + " :";
            for (const auto& tr : sh.traits) t += " [" + utf8(tr) + "]";
            for (const auto& l : sh.stats) t += " " + utf8(l.label) + "=" + utf8(l.value);
            AutoTestLog(t.c_str());
        }
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Camera.Camera().SetPitch(50.0f);
        m_Camera.Camera().distance = 14.0f;
        // 的はプレイヤーの正面（カメラの奥）8〜9m に動かない雑魚 3 体。前後の段で同じ所に当たるので見比べやすい
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        const Vector3 targets[] = { { 0.0f, 0.0f, 8.0f }, { -1.5f, 0.0f, 9.0f }, { 1.5f, 0.0f, 9.0f } };
        for (const Vector3& t : targets)
            m_Swarm.SpawnEnemy(Vector3(pp.x + t.x, gy, pp.z + t.z), 1000.0f, 0.0f);
        ClearBackpackItems(bp);
        // 2026-10-06：基本魔法が多マスになった（火球 2x2・メテオ 十字 5）ので 3x3 の枠には石弾（L 字 3 マス）を置く。
        // 石弾の命中（StoneShotHit：土煙 + 石）が「炸裂」の見た目。火球は爆発しなくなった
        const int a = BackpackLogic::Place(bp, ItemID::StoneShot, lo, lo, 0);
        bp.dirty = true;
        wand.castingPaused = false;
        char line[96];
        snprintf(line, sizeof(line), "magnifier A: stone %d (no magnifier)", a);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        logState("A");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 4.5f)
    {
        AutoTestLog("magnifier look A");   // 石弾が当たっている頃（外の撮影）
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 9.0f)
    {
        // 拡大鏡（斜めの影響マス）を、石弾に掛かる空きマスのどこかへ：3x3 の空きを順に試す
        int m = -1, mx = -1, mz = -1;
        for (int z = 0; z < 3 && m < 0; ++z)
            for (int x = 0; x < 3 && m < 0; ++x)
            {
                const int idx = BackpackLogic::Place(bp, ItemID::Magnifier, lo + x, lo + z, 0);
                if (idx < 0) continue;
                const std::vector<int> inf = BackpackLogic::GetInfluencers(bp, 0);   // 0 = 石弾
                bool hits = false;
                for (int k : inf) hits = hits || k == idx;
                if (hits) { m = idx; mx = x; mz = z; }
                else BackpackLogic::Remove(bp, idx);
            }
        bp.dirty = true;
        char line[96];
        snprintf(line, sizeof(line), "magnifier B: magnifier %d placed at %d,%d", m, mx, mz);
        AutoTestLog(line);
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 10.0f)
    {
        logState("B");
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5 && m_AutoTime >= 12.5f)
    {
        AutoTestLog("magnifier look B");
        m_AutoStep = 6;
    }
    else if (m_AutoStep == 6 && m_AutoTime >= 14.0f)
    {
        AutoTestLog("magnifier done");
        m_AutoStep = 7;
    }

    static float s_Timer = 0.0f;
    s_Timer += ImGui::GetIO().DeltaTime;
    if (s_Timer >= 1.0f)
    {
        s_Timer = 0.0f;
        const auto& c = m_Swarm.GetCounters();
        char line[96];
        snprintf(line, sizeof(line), "shots %u areas %u mobs %u", c.aliveProjectiles, c.aliveAreas, c.aliveEnemies);
        AutoTestLog(line);
    }
}

REGISTER_BATTLE_AUTOTEST("magnifier", AutoTestMagnifier)
