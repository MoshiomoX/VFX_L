// ============================================================
// AutoTestLifesteal.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=lifesteal
// 吸血のルーン（2026-10-25）：上下左右の魔法が敵に与えたダメージの 3% が HP に戻る（1 秒あたり最大 HP の 2% まで）。
// 1 秒: 湧き停止・全消し・MP 無限・自然回復 0、HP 100 のうち 20、正面 +Z 10〜13m に動かない的 5 体（HP 10 万）、
//       3x3 枠の中段左にホーミングボルト（横 2）。カメラは背後 12m・見下ろし 28 度
// A 段（1.5〜5.5 秒）：ルーン無し。HP は 20 のまま、吸血の累計も 0 のはず → `lifesteal A hp stolen`
// 5.5 秒：ホーミングボルトの上に吸血のルーン（十字の影響 = 弾の左のマス）→ `lifesteal spell lifesteal damage`、説明カード
// B 段（6〜14 秒）：0.5 秒毎 `lifesteal B t hp healed`。8.5 秒 `lifesteal look heal`。終わりに `lifesteal B result hp healed perSec`
// C 段（14〜20 秒）：回復の割合を 0.5 にして上限（2 / 秒）で頭打ちになるか → `lifesteal C result perSec cap`
// 20 秒 `lifesteal done`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

#include <Windows.h>

namespace
{
    class AutoTestLifesteal final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void SpawnTargets();
    };

    std::string Utf8(const std::wstring& w)
    {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string o(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
        return o;
    }
}

void AutoTestLifesteal::SpawnTargets()
{
    m_Swarm.KillAll();
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    const float gy = m_Swarm.GetAIParams().groundY;
    static const float kOff[5][2] = { { 0.0f, 10.0f }, { 2.5f, 10.5f }, { -2.5f, 11.0f }, { 1.5f, 13.0f }, { -1.5f, 13.5f } };
    for (const auto& o : kOff)
        m_Swarm.SpawnEnemy(Vector3(pp.x + o[0], m_Grid.SampleHeight(pp.x + o[0], pp.z + o[1]) + gy, pp.z + o[1]), 100000.0f, 0.0f);
}

void AutoTestLifesteal::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    const int lo = BackpackComponent::GRID / 2 - 1;
    static float s_HpAt = 0.0f, s_HealAt = 0.0f, s_NextLog = 0.0f;
    char line[256];

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<PlayerStatsComponent>(m_Player)) m_Registry.Get<PlayerStatsComponent>(m_Player).healthRegen = 0.0f;
        hp.max = 100.0f;
        hp.current = 20.0f;
        SpawnTargets();
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            if (BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo, 0) < 0)
                AutoTestLog("lifesteal place homing FAILED");
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.distance = 12.0f;
        cam.SetPitch(28.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        m_Swarm.ConsumeLifesteal();
        AutoTestLog("lifesteal start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 5.5f)
    {
        snprintf(line, sizeof(line), "lifesteal A hp %.2f healed %.2f", hp.current, m_Lifesteal.Total());
        AutoTestLog(line);
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        // 2026-10-27 吸血はルーンから設置枠へ：3x3 の下に吸血の枠（T 字）を敷き、ホーミングボルトを縦にして両方に跨らせる
        if (BackpackLogic::PlaceFrame(bp, ItemID::FrameLifesteal, lo + 3, lo, 0) < 0)
            AutoTestLog("lifesteal place frame FAILED");
        for (int i = (int)bp.items.size() - 1; i >= 0; --i)
            if (bp.items[i].id == ItemID::HomingBolt) BackpackLogic::Remove(bp, i);
        if (BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 2, lo + 1, 1) < 0)
            AutoTestLog("lifesteal place homing on frame FAILED");
        bp.dirty = true;
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 6.0f)
    {
        const auto& wand = m_Registry.Get<WandComponent>(m_Player);
        for (const auto& s : wand.spells)
        {
            snprintf(line, sizeof(line), "lifesteal spell lifesteal %.3f damage %.2f", s.lifesteal, s.damage);
            AutoTestLog(line);
        }
        // 説明カード：置いた弾と、ルーン
        const auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, i);
            if (sh.id != ItemID::HomingBolt && sh.id != ItemID::FrameLifesteal) continue;
            std::string l = "lifesteal sheet " + Utf8(sh.title) + " : " + Utf8(sh.description);
            for (const auto& t : sh.traits) l += " [" + Utf8(t) + "]";
            for (const auto& s : sh.stats) l += " {" + Utf8(s.label) + " " + Utf8(s.value) + "}";
            l += " footer " + Utf8(sh.footer);
            AutoTestLog(l.c_str());
        }
        s_HpAt = hp.current;
        s_HealAt = m_Lifesteal.Total();
        s_NextLog = m_AutoTime + 0.5f;
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3)
    {
        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog += 0.5f;
            snprintf(line, sizeof(line), "lifesteal B t %.2f hp %.2f healed %.2f", m_AutoTime - 6.0f, hp.current, m_Lifesteal.Total());
            AutoTestLog(line);
        }
        static bool s_Looked = false;
        if (!s_Looked && m_AutoTime >= 8.5f)
        {
            s_Looked = true;
            AutoTestLog("lifesteal look heal");
        }
        if (m_AutoTime >= 14.0f)
        {
            const float healed = m_Lifesteal.Total() - s_HealAt;
            snprintf(line, sizeof(line), "lifesteal B result hp %.2f -> %.2f healed %.2f perSec %.3f", s_HpAt, hp.current, healed, healed / 8.0f);
            AutoTestLog(line);
            // C：割合を 0.5 に（上限 2 / 秒で止まるか）
            hp.current = 20.0f;
            m_Lifesteal.ratio = 0.5f;
            s_HealAt = m_Lifesteal.Total();
            m_AutoStep = 4;
        }
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 20.0f)
    {
        const float healed = m_Lifesteal.Total() - s_HealAt;
        snprintf(line, sizeof(line), "lifesteal C result hp %.2f healed %.2f perSec %.3f cap %.3f", hp.current, healed, healed / 6.0f, m_Lifesteal.capPerSec * hp.max);
        AutoTestLog(line);
        m_Lifesteal.ratio = Swarm::kLifestealRatio;
        // 2026-10-27：効果のある枠を全部敷いてバックパックを開く（見た目の確認）+ 枠の説明
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            if (BackpackLogic::PlaceFrame(bp, ItemID::FrameArea, lo - 2, lo, 0) < 0) AutoTestLog("lifesteal place FrameArea FAILED");
            if (BackpackLogic::PlaceFrame(bp, ItemID::FrameFocus, lo, lo + 3, 0) < 0) AutoTestLog("lifesteal place FrameFocus FAILED");
            if (BackpackLogic::PlaceFrame(bp, ItemID::FrameHaste, lo + 1, lo - 2, 0) < 0) AutoTestLog("lifesteal place FrameHaste FAILED");
            if (BackpackLogic::Place(bp, ItemID::Fireball, lo - 1, lo, 0) < 0) AutoTestLog("lifesteal place fireball FAILED");   // 3x3 と範囲の枠に跨る
            bp.dirty = true;
            for (ItemID f : { ItemID::FrameArea, ItemID::FrameFocus, ItemID::FrameHaste, ItemID::FrameLifesteal, ItemID::FrameCross })
            {
                const ItemInfo::Sheet sh = ItemInfo::Describe(f);
                std::string l = "lifesteal frame " + Utf8(sh.title) + " (" + Utf8(sh.category) + ") : " + Utf8(sh.description);
                for (const auto& t : sh.traits) l += " [" + Utf8(t) + "]";
                for (const auto& s : sh.stats) l += " {" + Utf8(s.label) + " " + Utf8(s.value) + "}";
                AutoTestLog(l.c_str());
            }
            for (int i = 0; i < (int)bp.items.size(); ++i)
                if (bp.items[i].id == ItemID::Fireball)
                {
                    const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, i);
                    std::string l = "lifesteal sheet " + Utf8(sh.title);
                    for (const auto& s : sh.stats) l += " {" + Utf8(s.label) + " " + Utf8(s.value) + "}";
                    l += " footer " + Utf8(sh.footer);
                    AutoTestLog(l.c_str());
                }
            m_GameUI.TestShow(1);
            AutoTestLog("lifesteal look bag");
        }
        AutoTestLog("lifesteal done");
        m_AutoStep = 5;
    }
}

REGISTER_BATTLE_AUTOTEST("lifesteal", AutoTestLifesteal);
