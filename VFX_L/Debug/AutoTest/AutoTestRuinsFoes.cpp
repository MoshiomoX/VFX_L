// ============================================================
// AutoTestRuinsFoes.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=ruinsfoes（遺跡の敵。2026-10-25）
//   A 呪いの鎧：正面 7m に 1 体（HP 5・動かない）→ 撃つ → 崩れて（死体は残らない）→ 3 秒後に蘇った鎧（HP 半分・歩く）→ 倒す
//   B 石像ガーゴイル：正面 12m に石像 → プレイヤーが近付く（7m で目覚める）→ 飛ぶ → 急降下で当たる → 着地 → また飛ぶ
//   C 死霊術師：正面 8〜10m に雑魚 4 体（HP 1）を倒し、その奥 12m に術師 → 詠唱 → 死体の所に骸骨
//   D 鎖の番人：正面 9m → 溜め（地面の帯）→ 鎖が当たって番人の前へ引き寄せられる
//   E 鎖の番人を横へ走って避ける（向きは溜めの頭で固定）
//   F 6 種を並べて正面 / 斜め後ろから撮る（UI を隠す）。VFXL_RUINS_LOOK=1 で F だけ
// 0.25 秒毎 `ruinsfoes <段> t kinds… dist hp lost`、見どころで `ruinsfoes look <名>`（自撮り VFXL_SELF_SHOT）、各段の終わりに `result`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestRuinsFoes final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };

    struct KindCount { int armor = 0, armor2 = 0, gargoyle = 0, necro = 0, skeleton = 0, warden = 0, other = 0; };
}

void AutoTestRuinsFoes::Run()
{
    char line[260];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();
    static const char* s_Phase = "-";
    static float s_Base = 0.0f, s_NextLog = 0.0f, s_LastHp = 0.0f, s_Lost = 0.0f;
    static Vector3 s_Origin;
    static int s_Hits = 0;
    static bool s_Look[16] = {};
    static uint32_t s_Wakes0 = 0, s_Dives0 = 0, s_Revived0 = 0, s_Raised0 = 0, s_Throws0 = 0, s_Chains0 = 0;
    static float s_EventAt = -1.0f;
    static std::vector<Vector3> s_Corpses;
    const float groundY = m_Swarm.GetAIParams().groundY;

    auto spawnAt = [&](float dx, float dz, uint32_t kind, float hp, float speed)
    {
        const float x = s_Origin.x + dx, z = s_Origin.z + dz;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, kind, std::atan2(-dx, -dz));
    };
    auto look = [&](int id, const char* name)
    {
        if (s_Look[id]) return;
        s_Look[id] = true;
        snprintf(line, sizeof(line), "ruinsfoes look %s", name);
        AutoTestLog(line);
    };
    auto aim = [&](float yawDeg, float pitch, float dist)
    {
        cam.SetYaw(yawDeg);
        cam.SetPitch(pitch);
        cam.distance = dist;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
    };
    auto begin = [&](const char* ph, bool casting)
    {
        s_Phase = ph;
        m_Swarm.KillAll();
        m_Mobs.ClearRuinsPending();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = !casting;
        pcs.testMove = Vector2::Zero;
        tf.position = Vector3(s_Origin.x, m_Grid.SampleHeight(s_Origin.x, s_Origin.z) + 1.0f, s_Origin.z);
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        s_Base = m_AutoTime;
        s_Lost = 0.0f;
        s_Hits = 0;
        s_EventAt = -1.0f;
        for (bool& b : s_Look) b = false;
        s_Wakes0 = m_Mobs.GetGargoyleWakes();
        s_Dives0 = m_Mobs.GetGargoyleDives();
        s_Revived0 = m_Mobs.GetArmorsRevived();
        s_Raised0 = m_Mobs.GetSkeletonsRaised();
        s_Throws0 = m_Mobs.GetChainThrowsSeen();
        s_Chains0 = m_ChainHits;
        snprintf(line, sizeof(line), "ruinsfoes %s begin", ph);
        AutoTestLog(line);
    };
    auto count = [&](KindCount& kc, float& nearDist, float& nearY, float& nearFuse)
    {
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        nearDist = -1.0f; nearY = 0.0f; nearFuse = 0.0f;
        if (!m_Swarm.DebugReadEnemies(en, st, &ex)) return false;
        for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
        {
            if (st[i] == Swarm::kStateDead) continue;
            switch (ex[i].kind)
            {
            case Swarm::kEnemyKindArmor: ++kc.armor; break;
            case Swarm::kEnemyKindArmorRevived: ++kc.armor2; break;
            case Swarm::kEnemyKindGargoyle: ++kc.gargoyle; break;
            case Swarm::kEnemyKindNecro: ++kc.necro; break;
            case Swarm::kEnemyKindSkeleton: ++kc.skeleton; break;
            case Swarm::kEnemyKindWarden: ++kc.warden; break;
            default: ++kc.other; break;
            }
            const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (ex[i].kind >= Swarm::kEnemyKindArmor && (nearDist < 0.0f || d < nearDist))
            {
                nearDist = d;
                nearY = en[i].position.y - m_Grid.SampleHeight(en[i].position.x, en[i].position.z) - groundY;
                nearFuse = ex[i].fuse;
            }
        }
        return true;
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
            s_LastHp = hp.current;
        }
        s_Origin = tf.position;
        pcs.testInput = true;
        aim(20.0f, 26.0f, 8.0f);
        char only[8] = {};
        if (GetEnvironmentVariableA("VFXL_RUINS_LOOK", only, sizeof(only)) > 0 && only[0] == '1')
        {
            m_AutoStep = 6;
            s_Base = m_AutoTime;
            return;
        }
        begin("A", false);
        spawnAt(0.0f, 7.0f, Swarm::kEnemyKindArmor, 5.0f, 0.0f);
        m_AutoStep = 1;
        return;
    }
    if (m_AutoStep == 0 || m_AutoStep >= 7) return;

    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f) { s_Lost += s_LastHp - hp.current; ++s_Hits; }
        s_LastHp = hp.current;
    }
    const float t = m_AutoTime - s_Base;

    KindCount kc;
    float nearDist = -1.0f, nearY = 0.0f, nearFuse = 0.0f;
    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.25f;
        if (count(kc, nearDist, nearY, nearFuse))
        {
            snprintf(line, sizeof(line), "ruinsfoes %s t %.2f armor %d revived %d gargoyle %d necro %d skeleton %d warden %d other %d near %.1f y %.2f fuse %.2f hits %d lost %.0f",
                s_Phase, t, kc.armor, kc.armor2, kc.gargoyle, kc.necro, kc.skeleton, kc.warden, kc.other, nearDist, nearY, nearFuse, s_Hits, s_Lost);
            AutoTestLog(line);
        }
    }

    // ---- A 呪いの鎧 ----
    if (m_AutoStep == 1)
    {
        if (t >= 1.2f) look(0, "armor");
        if (t >= 1.6f && m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        if (m_Mobs.GetArmorsRevived() > s_Revived0 && s_EventAt < 0.0f) s_EventAt = t;
        if (t >= 3.4f) look(1, "armorpile");   // 崩れて 1 秒くらい（撃ち始めてすぐ倒れる）
        if (s_EventAt >= 0.0f && t >= s_EventAt + 0.15f) look(2, "armorrise");
        if (s_EventAt >= 0.0f && t >= s_EventAt + 1.2f) look(3, "armorrevived");
        if (t >= 11.0f)
        {
            snprintf(line, sizeof(line), "ruinsfoes A result revived %u revivedAt %.2f", m_Mobs.GetArmorsRevived() - s_Revived0, s_EventAt);
            AutoTestLog(line);
            begin("B", false);
            aim(35.0f, 24.0f, 11.0f);
            spawnAt(0.0f, 12.0f, Swarm::kEnemyKindGargoyle, 100000.0f, 4.5f);
            m_AutoStep = 2;
        }
        return;
    }
    // ---- B 石像ガーゴイル ----
    if (m_AutoStep == 2)
    {
        if (t >= 1.0f) look(0, "statue");
        // 2 秒から前へ歩く（7m で目覚める）→ 目覚めたら止まる
        const bool woke = m_Mobs.GetGargoyleWakes() > s_Wakes0;
        pcs.testMove = (t >= 2.0f && !woke) ? Vector2(0.0f, 0.5f) : Vector2::Zero;
        if (woke && s_EventAt < 0.0f) s_EventAt = t;
        if (s_EventAt >= 0.0f)
        {
            if (t >= s_EventAt + 0.25f) look(1, "wake");
            if (t >= s_EventAt + 1.6f) look(2, "fly");
        }
        static float s_DiveAt = -1.0f;
        if (t < 0.1f) s_DiveAt = -1.0f;
        if (m_Mobs.GetGargoyleDives() > s_Dives0 && s_DiveAt < 0.0f) s_DiveAt = t;
        if (s_DiveAt >= 0.0f)
        {
            if (t >= s_DiveAt + 0.3f) look(3, "divewind");
            if (t >= s_DiveAt + 0.72f) look(4, "dive");
            if (t >= s_DiveAt + 1.4f) look(5, "landed");
        }
        if (t >= 16.0f)
        {
            snprintf(line, sizeof(line), "ruinsfoes B result wakes %u wokeAt %.2f dives %u hits %d lost %.0f",
                m_Mobs.GetGargoyleWakes() - s_Wakes0, s_EventAt, m_Mobs.GetGargoyleDives() - s_Dives0, s_Hits, s_Lost);
            AutoTestLog(line);
            begin("C", true);
            aim(25.0f, 30.0f, 12.0f);
            s_Corpses.clear();
            for (int k = 0; k < 4; ++k)
            {
                const float dx = -2.4f + 1.6f * (float)k, dz = 8.0f + (float)(k & 1);
                spawnAt(dx, dz, Swarm::kEnemyKindMob, 1.0f, 0.0f);
                s_Corpses.push_back(Vector3(s_Origin.x + dx, 0.0f, s_Origin.z + dz));
            }
            m_AutoStep = 3;
        }
        return;
    }
    // ---- C 死霊術師 ----
    if (m_AutoStep == 3)
    {
        if (t >= 2.5f && !s_Look[15])
        {
            s_Look[15] = true;   // 雑魚を倒し終えた所で術師を出す（撃つのを止める = 術師は倒さない）
            if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
            spawnAt(0.0f, 12.0f, Swarm::kEnemyKindNecro, 100000.0f, 3.0f);
        }
        if (t >= 3.4f) look(0, "necro");
        if (m_Mobs.GetSkeletonsRaised() > s_Raised0 && s_EventAt < 0.0f) s_EventAt = t;
        if (t >= 3.2f + m_Swarm.GetBomberParams().necroWindup) look(1, "raisemark");
        if (s_EventAt >= 0.0f && t >= s_EventAt + 0.6f) look(2, "skeletons");
        if (t >= 12.0f)
        {
            // 骸骨がどこに立ったか（倒した雑魚の所から最寄り）
            std::vector<Swarm::Enemy> en;
            std::vector<uint32_t> st;
            std::vector<Swarm::EnemyExtra> ex;
            int nearCorpse = 0;
            if (m_Swarm.DebugReadEnemies(en, st, &ex))
                for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
                    if (st[i] != Swarm::kStateDead && ex[i].kind == Swarm::kEnemyKindSkeleton)
                        for (const Vector3& c : s_Corpses)
                            if (std::fabs(en[i].position.x - c.x) < 1.0f && std::fabs(en[i].position.z - c.z) < 1.0f) { ++nearCorpse; break; }
            snprintf(line, sizeof(line), "ruinsfoes C result raised %u raisedAt %.2f skeletonsAtCorpse(alive, after walking) %d",
                m_Mobs.GetSkeletonsRaised() - s_Raised0, s_EventAt, nearCorpse);
            AutoTestLog(line);
            begin("D", false);
            aim(70.0f, 28.0f, 11.0f);
            spawnAt(0.0f, 9.0f, Swarm::kEnemyKindWarden, 100000.0f, 2.8f);
            m_AutoStep = 4;
        }
        return;
    }
    // ---- D 鎖の番人（立ったまま当たる）----
    if (m_AutoStep == 4)
    {
        if (t >= 0.6f) look(0, "wardenwind");
        if (m_Mobs.GetChainThrowsSeen() > s_Throws0 && s_EventAt < 0.0f) s_EventAt = t;
        if (s_EventAt >= 0.0f && t >= s_EventAt + 0.15f) look(1, "chain");
        static float s_HitAt = -1.0f, s_DistBefore = 0.0f;
        if (t < 0.1f) { s_HitAt = -1.0f; s_DistBefore = 0.0f; }
        if (s_EventAt < 0.0f && nearDist > 0.0f) s_DistBefore = nearDist;
        if (m_ChainHits > s_Chains0 && s_HitAt < 0.0f) s_HitAt = t;
        if (s_HitAt >= 0.0f && t >= s_HitAt + 0.12f) look(2, "pull");
        if (s_HitAt >= 0.0f && t >= s_HitAt + 0.6f && !s_Look[14])
        {
            s_Look[14] = true;
            float dx = 0.0f;
            KindCount k2;
            float d2 = -1.0f, y2 = 0.0f, f2 = 0.0f;
            count(k2, d2, y2, f2);
            (void)dx;
            snprintf(line, sizeof(line), "ruinsfoes D pulled distBefore %.1f distAfter %.1f", s_DistBefore, d2);
            AutoTestLog(line);
        }
        if (t >= 8.0f)
        {
            snprintf(line, sizeof(line), "ruinsfoes D result throws %u chainHits %u throwAt %.2f hitAt %.2f lost %.0f",
                m_Mobs.GetChainThrowsSeen() - s_Throws0, m_ChainHits - s_Chains0, s_EventAt, s_HitAt, s_Lost);
            AutoTestLog(line);
            begin("E", false);
            spawnAt(0.0f, 9.0f, Swarm::kEnemyKindWarden, 100000.0f, 2.8f);
            m_AutoStep = 5;
        }
        return;
    }
    // ---- E 溜め（番人の fuse > 0）を見たら 1.3 秒横へ走る（投げる度に左右を入れ替える）----
    if (m_AutoStep == 5)
    {
        static float s_DodgeUntil = -1.0f, s_DodgeDir = 1.0f;
        if (t < 0.1f) { s_DodgeUntil = -1.0f; s_DodgeDir = 1.0f; }
        if (nearFuse > 0.0f && t > s_DodgeUntil + 0.5f)
        {
            s_DodgeUntil = t + 1.3f;
            s_DodgeDir = -s_DodgeDir;
            snprintf(line, sizeof(line), "ruinsfoes E dodge t %.2f fuse %.2f", t, nearFuse);
            AutoTestLog(line);
        }
        pcs.testMove = (t < s_DodgeUntil) ? Vector2(s_DodgeDir, 0.0f) : Vector2::Zero;
        if (s_DodgeUntil > 0.0f && t >= s_DodgeUntil - 0.6f) look(0, "dodge");
        if (t >= 14.0f)
        {
            pcs.testMove = Vector2::Zero;
            snprintf(line, sizeof(line), "ruinsfoes E result throws %u chainHits %u", m_Mobs.GetChainThrowsSeen() - s_Throws0, m_ChainHits - s_Chains0);
            AutoTestLog(line);
            m_AutoStep = 6;
            s_Base = m_AutoTime;
        }
        return;
    }
    // ---- F 近くで並べて見た目（動かない = ガーゴイルは石像のまま、正面 → 斜め後ろ）----
    if (m_AutoStep == 6)
    {
        if (t >= 0.3f && !s_Look[15])
        {
            s_Look[15] = true;
            begin("F", false);
            const uint32_t kinds[6] = { Swarm::kEnemyKindArmor, Swarm::kEnemyKindArmorRevived, Swarm::kEnemyKindGargoyle,
                                        Swarm::kEnemyKindNecro, Swarm::kEnemyKindSkeleton, Swarm::kEnemyKindWarden };
            for (int k = 0; k < 6; ++k)
                spawnAt(-4.0f + 1.6f * (float)k, 5.0f, kinds[k], 100000.0f, 0.0f);
            // カメラは列の真ん中を見る（正面 = プレイヤー側から、後ろ = 列の向こうから）
            static Vector3 s_Row;
            s_Row = Vector3(s_Origin.x, m_Grid.SampleHeight(s_Origin.x, s_Origin.z + 5.0f) + 1.2f, s_Origin.z + 5.0f);
            m_Camera.SetTargetOverride(&s_Row);
            // プレイヤーは画面の外へよける、UI も隠す（並びを見るだけの絵）
            tf.position = Vector3(s_Origin.x + 9.0f, m_Grid.SampleHeight(s_Origin.x + 9.0f, s_Origin.z - 3.0f) + 1.0f, s_Origin.z - 3.0f);
            m_GameUI.hideAll = true;
            aim(0.0f, 10.0f, 7.5f);
            s_Look[15] = true;
            return;
        }
        if (s_Look[15] && t >= 1.2f) look(0, "lineup");
        if (s_Look[15] && t >= 1.4f && !s_Look[13]) { s_Look[13] = true; aim(165.0f, 16.0f, 7.0f); }
        if (s_Look[15] && t >= 2.2f) look(1, "lineupback");
        if (s_Look[15] && t >= 2.6f)
        {
            m_Camera.SetTargetOverride(nullptr);
            m_GameUI.hideAll = false;
            pcs.testInput = false;
            AutoTestLog("ruinsfoes done");
            m_AutoStep = 7;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("ruinsfoes", AutoTestRuinsFoes)
