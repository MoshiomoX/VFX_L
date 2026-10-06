// ============================================================
// AutoTestCrystal.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=crystal
// 水晶玉（召喚物、2026-10-06）：貯蔵された魔法を光球が撃つか・杖は撃たなくなるか・ルーンと消費 MP の倍率が合うか。
//   1 秒: 湧き停止・全消し・無敵・MP 無限、正面 +Z 8〜9m に動かない的 3 体、9x9 全部に枠。
//        バックパック: 水晶玉 (4,4)、左に ホーミングボルト (4,2)〜(4,3)（横 2 → 右端が水晶玉の左マス）、
//        右に メテオ (4,6)（十字 → 左端 (4,5) が水晶玉の右マス）、ホーミングボルトの上に 分裂のルーン (3,2)、
//        右下に ファイアボール (7,7)（貯蔵されない対照）。
//   2 秒: 集約結果（storeUnit / MP / 弾数）を記録。0.5 秒毎に "crystal t orbs orbCasts proj"。
//   3.5 / 5.5 / 9 秒 "crystal look <n>"、12 秒 "crystal done"
// 期待: ホーミングボルト storeUnit 0・MP 10.5×1.5 = 15.75・弾数 2（分裂）、メテオ storeUnit 0・triggered 0、
//       ファイアボール storeUnit -1、orbs 1 つ（stored 2）、光球は 2 秒毎に 1 個 → 最大 3 個、orbCasts が増える
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "ECS/System/WeaponSystem.h"

namespace
{
    class AutoTestCrystal final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

void AutoTestCrystal::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& cam = m_Camera.Camera();
    static float s_NextSample = 1.5f;
    static int s_Look = 0;
    static const float kLooks[] = { 3.5f, 5.5f, 9.0f };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        m_Swarm.SpawnEnemy(Vector3(pp.x, m_Grid.SampleHeight(pp.x, pp.z + 8.0f) + gy, pp.z + 8.0f), 100000.0f, 0.0f);
        m_Swarm.SpawnEnemy(Vector3(pp.x - 1.5f, m_Grid.SampleHeight(pp.x - 1.5f, pp.z + 9.0f) + gy, pp.z + 9.0f), 100000.0f, 0.0f);
        m_Swarm.SpawnEnemy(Vector3(pp.x + 1.5f, m_Grid.SampleHeight(pp.x + 1.5f, pp.z + 9.0f) + gy, pp.z + 9.0f), 100000.0f, 0.0f);

        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            for (int r = 0; r < BackpackComponent::GRID; r += 3)
                for (int c = 0; c < BackpackComponent::GRID; c += 3)
                    if (BackpackLogic::CanPlaceFrame(bp, ItemID::Frame3x3, r, c, 0))
                        BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
            auto place = [&](ItemID id, int r, int c)
                {
                    const int idx = BackpackLogic::CanPlace(bp, id, r, c, 0) ? BackpackLogic::Place(bp, id, r, c, 0) : -1;
                    char line[128];
                    snprintf(line, sizeof(line), "crystal place %s at %d,%d -> %d", ItemDatabase::GetCommon(id)->name, r, c, idx);
                    AutoTestLog(line);
                };
            place(ItemID::CrystalBall, 4, 4);
            place(ItemID::HomingBolt, 4, 2);
            place(ItemID::Meteor, 4, 6);
            place(ItemID::SplitRune, 3, 2);
            place(ItemID::Fireball, 7, 7);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        cam.SetYaw(0.0f);
        cam.distance = 9.0f;
        cam.SetPitch(30.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("crystal start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        const auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        const auto& wand = m_Registry.Get<WandComponent>(m_Player);
        char line[200];
        for (const SpellStats& s : wand.spells)
        {
            snprintf(line, sizeof(line), "crystal spell %s storeUnit %d triggered %d mana %.2f count %d interval %.3f",
                ItemDatabase::GetCommon(s.id)->name, s.storeUnit, s.triggered ? 1 : 0, s.manaCost, s.projectileCount, s.castInterval);
            AutoTestLog(line);
        }
        for (const AreaStats& a : wand.areas)
        {
            snprintf(line, sizeof(line), "crystal area %s storeUnit %d triggered %d mana %.2f",
                ItemDatabase::GetCommon(a.id)->name, a.storeUnit, a.triggered ? 1 : 0, a.manaCost);
            AutoTestLog(line);
        }
        for (size_t u = 0; u < wand.orbs.size(); ++u)
        {
            const OrbUnitStats& o = wand.orbs[u];
            snprintf(line, sizeof(line), "crystal unit %d stored %d maxOrbs %d interval %.1f life %.1f color %.2f %.2f %.2f",
                (int)u, o.storedCount, o.maxOrbs, o.orbInterval, o.orbLife, o.color.x, o.color.y, o.color.z);
            AutoTestLog(line);
        }
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            snprintf(line, sizeof(line), "crystal item %s storedBy %d triggerReady %d",
                ItemDatabase::GetCommon(bp.items[i].id)->name, BackpackLogic::StoredBy(bp, i), BackpackLogic::IsTriggerReady(bp, i) ? 1 : 0);
            AutoTestLog(line);
        }
        // 説明カード（貯蔵中の行と MP）
        for (int i = 0; i < (int)bp.items.size(); ++i)
        {
            const ItemInfo::Sheet sh = ItemInfo::DescribePlaced(bp, i);
            std::string t;
            for (const auto& w : sh.traits) { t += " | "; for (wchar_t ch : w) t += (ch < 128) ? (char)ch : '?'; }
            snprintf(line, sizeof(line), "crystal sheet %s traits %d footer %d", ItemDatabase::GetCommon(bp.items[i].id)->name, (int)sh.traits.size(), (int)sh.footer.size());
            AutoTestLog(line);
        }
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2)
    {
        if (m_AutoTime >= s_NextSample)
        {
            const auto& c = m_Swarm.GetCounters();
            Vector3 op;
            const bool hasOrb = m_WeaponSystem.GetOrbPos(0, op);
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            char line[200];
            snprintf(line, sizeof(line), "crystal t %.1f orbs %d orbCasts %u proj %u areas %u orb0 (%.2f %.2f %.2f) fromPlayer %.2f",
                m_AutoTime, m_WeaponSystem.GetOrbCount(), m_WeaponSystem.GetOrbCasts(), c.aliveProjectiles, c.aliveAreas,
                op.x, op.y, op.z, hasOrb ? (op - pp).Length() : -1.0f);
            AutoTestLog(line);
            s_NextSample += 0.5f;
        }
        if (s_Look < 3 && m_AutoTime >= kLooks[s_Look])
        {
            char line[64];
            snprintf(line, sizeof(line), "crystal look %d", s_Look);
            AutoTestLog(line);
            ++s_Look;
        }
        if (m_AutoTime >= 12.0f) { AutoTestLog("crystal done"); m_AutoStep = 3; }
    }
}

REGISTER_BATTLE_AUTOTEST("crystal", AutoTestCrystal)
