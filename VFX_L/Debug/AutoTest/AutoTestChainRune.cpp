// ============================================================
// AutoTestChainRune.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=chainrune
// 連鎖のルーン（2026-10-16）: ホーミングボルトだけ / + 連鎖のルーン で、正面の群れ 5 体の減った HP と
// ダメージを受けた体数を比べる。ルーン無しは最寄りの 1 体だけ、ルーン有りは 1 発が 3 体に当たる（0.75 × 3 = 2.25 倍）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestChainRune final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void SpawnTargets();
        bool ReadHp(float* total, int* damaged);
    };
}

// ============================================================
// TEMP-TEST: 連鎖のルーン（VFXL_BATTLE_AUTOTEST=chainrune）
// 1 秒: 湧き停止・全消し・無敵・MP 無限、正面 +Z 10〜13m に動かない的 5 体（HP 10 万）、
//       3x3 枠にホーミングボルトだけ（横 2 マス、枠の中段左）。カメラは背後 14m・見下ろし 30 度
// A 段（1.5〜6.5 秒）: ルーン無しで 5 秒撃つ → "chainrune A lost <HP> damaged <体数>"
// 7 秒: 的を置き直し、ホーミングボルトの上に連鎖のルーン（上下 2 マスの影響 = 弾の左のマス）→ "chainrune spell chain <回数> damage <威力>"
// B 段（7.5〜12.5 秒）: 同じく 5 秒 → "chainrune B lost <HP> damaged <体数> ratio <B/A>"、途中 8.5 / 9.5 / 10.5 秒に "chainrune look <n>"
// 13 秒 "chainrune done"
// ============================================================
void AutoTestChainRune::SpawnTargets()
{
    m_Swarm.KillAll();
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    const float gy = m_Swarm.GetAIParams().groundY;
    // 群れ: 前方 10〜13.5m、横 ±2.5m。連鎖の届く範囲（8m）に全員入る
    static const float kOff[5][2] = { { 0.0f, 10.0f }, { 2.5f, 10.5f }, { -2.5f, 11.0f }, { 1.5f, 13.0f }, { -1.5f, 13.5f } };
    for (const auto& o : kOff)
        m_Swarm.SpawnEnemy(Vector3(pp.x + o[0], gy, pp.z + o[1]), 100000.0f, 0.0f);
}

bool AutoTestChainRune::ReadHp(float* total, int* damaged)
{
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    if (!m_Swarm.DebugReadEnemies(en, st)) return false;
    *total = 0.0f;
    *damaged = 0;
    for (size_t i = 0; i < en.size() && i < st.size(); ++i)
    {
        if (st[i] == Swarm::kStateDead) continue;
        const float hp = Swarm::HpFromFixed(en[i].hp);
        *total += hp;
        if (hp < 100000.0f - 0.5f) ++*damaged;
    }
    return true;
}

void AutoTestChainRune::Run()
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
    static float s_LostA = 0.0f;
    static int s_Look = 0;
    const int lo = BackpackComponent::GRID / 2 - 1;
    char line[160];

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        SpawnTargets();
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            // 中段の左 2 マス（横 2）。ルーンは後でその左のマスの上に置く
            if (BackpackLogic::Place(bp, ItemID::HomingBolt, lo + 1, lo, 0) < 0)
                AutoTestLog("chainrune place homing FAILED");
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        cam.SetYaw(0.0f);
        cam.distance = 14.0f;
        cam.SetPitch(30.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("chainrune start");
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 6.5f)
    {
        int damaged = 0;
        if (ReadHp(&s_LostA, &damaged))
        {
            s_LostA = 5.0f * 100000.0f - s_LostA;
            snprintf(line, sizeof(line), "chainrune A lost %.1f damaged %d", s_LostA, damaged);
            AutoTestLog(line);
        }
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 7.0f)
    {
        SpawnTargets();
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        if (BackpackLogic::Place(bp, ItemID::ChainRune, lo, lo, 0) < 0)
            AutoTestLog("chainrune place rune FAILED");
        bp.dirty = true;
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 7.5f)
    {
        // 集約は dirty の次のフレームなので 0.5 秒待ってから読む
        const auto& wand = m_Registry.Get<WandComponent>(m_Player);
        for (const auto& s : wand.spells)
        {
            snprintf(line, sizeof(line), "chainrune spell chain %d damage %.2f interval %.3f", s.chainCount, s.damage, s.castInterval);
            AutoTestLog(line);
        }
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4)
    {
        if (s_Look < 3 && m_AutoTime >= 8.5f + 1.0f * (float)s_Look)
        {
            const auto& c = m_Swarm.GetCounters();
            snprintf(line, sizeof(line), "chainrune look %d proj %u", s_Look, c.aliveProjectiles);
            AutoTestLog(line);
            ++s_Look;
        }
        if (m_AutoTime >= 12.5f)
        {
            float total = 0.0f;
            int damaged = 0;
            if (ReadHp(&total, &damaged))
            {
                const float lostB = 5.0f * 100000.0f - total;
                snprintf(line, sizeof(line), "chainrune B lost %.1f damaged %d ratio %.2f", lostB, damaged,
                    (s_LostA > 0.5f) ? lostB / s_LostA : 0.0f);
                AutoTestLog(line);
            }
            m_AutoStep = 5;
        }
    }
    // ---- C / D 段：アークボルト（2026-10-20、稲妻も跳ぶ）。置き方は Z 字を枠の左上に、ルーンはその下 ----
    else if (m_AutoStep == 5 && m_AutoTime >= 13.0f)
    {
        SpawnTargets();
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ClearBackpackItems(bp);
        // Z 字 {0,0}{0,1}{1,1}{1,2} を (lo, lo) に。ルーンは (lo+2, lo+1)：その上のマス (lo+1, lo+1) が稲妻
        if (BackpackLogic::Place(bp, ItemID::ArcBolt, lo, lo, 0) < 0)
            AutoTestLog("chainrune place arc FAILED");
        bp.dirty = true;
        AutoTestLog("chainrune C start (arc bolt, no rune)");
        m_AutoStep = 6;
    }
    else if (m_AutoStep == 6 && m_AutoTime >= 18.5f)
    {
        int damaged = 0;
        if (ReadHp(&s_LostA, &damaged))
        {
            s_LostA = 5.0f * 100000.0f - s_LostA;
            snprintf(line, sizeof(line), "chainrune C lost %.1f damaged %d", s_LostA, damaged);
            AutoTestLog(line);
        }
        SpawnTargets();
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        if (BackpackLogic::Place(bp, ItemID::ChainRune, lo + 2, lo + 1, 0) < 0)
            AutoTestLog("chainrune place rune (arc) FAILED");
        bp.dirty = true;
        m_AutoStep = 7;
    }
    else if (m_AutoStep == 7 && m_AutoTime >= 19.0f)
    {
        const auto& wand = m_Registry.Get<WandComponent>(m_Player);
        for (const auto& a : wand.areas)
        {
            snprintf(line, sizeof(line), "chainrune area chain %d damage %.2f interval %.3f", a.chainCount, a.damagePerTick, a.castInterval);
            AutoTestLog(line);
        }
        s_Look = 0;
        m_AutoStep = 8;
    }
    else if (m_AutoStep == 8)
    {
        if (s_Look < 3 && m_AutoTime >= 20.0f + 1.0f * (float)s_Look)
        {
            snprintf(line, sizeof(line), "chainrune look arc%d beams %d", s_Look, m_WeaponSystem.GetActiveBeamCount());
            AutoTestLog(line);
            ++s_Look;
        }
        if (m_AutoTime >= 24.0f)
        {
            float total = 0.0f;
            int damaged = 0;
            if (ReadHp(&total, &damaged))
            {
                const float lostD = 5.0f * 100000.0f - total;
                snprintf(line, sizeof(line), "chainrune D lost %.1f damaged %d ratio %.2f", lostD, damaged,
                    (s_LostA > 0.5f) ? lostD / s_LostA : 0.0f);
                AutoTestLog(line);
            }
            m_AutoStep = 9;
        }
    }
    else if (m_AutoStep == 9 && m_AutoTime >= 24.5f)
    {
        AutoTestLog("chainrune done");
        m_AutoStep = 10;
    }
}

REGISTER_BATTLE_AUTOTEST("chainrune", AutoTestChainRune)
