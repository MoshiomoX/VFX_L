// ============================================================
// AutoTestDashBomber.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=dashbomber
// 突撃爆弾兵（kEnemyKindDashBomber。2026-10-18）：プレイヤーが近づくと点火して突っ込み、触れた瞬間に爆発。
// 点火中は無敵（自爆兵も）。A 撃たずに 1 体を迎える、B 撃ちながら 1 体、C 撃ちながら自爆兵 1 体（点火後の HP が減らないか）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestDashBomber final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・HP 10 万・詠唱停止、正面 +Z 14m に突撃爆弾兵 1 体、カメラ背後 12m / 28°
// 0.25 秒毎 `dashbomber <A|B|C> t fuse dist speed hp alive hits lost`（fuse > 0 = 点火中、speed = その敵の水平速度）。
//   点火を初めて見たら `look <ph>lit`、爆発（alive 0 か HP が減った）の 0.1 秒後 `look <ph>blast`、各段の終わりに `result`
// 7 秒: B = 同じ配置で詠唱を戻す（点火後に撃っても HP が減らないか：`hpAtLit` と爆発直前の HP を比べる）
// 13 秒: C = 正面 3m に自爆兵 1 体（触れて点火 → 1 秒燃える間に撃たれても HP が減らないか）
// 18 秒: D = 正面 8m に 1 体（9m の点火範囲の中）。点火を見た瞬間プレイヤーを −Z へ 20m 飛ばす = 触れられない。
//         導火線 dashBomberFuse 秒で勝手に爆発して消えるか（blastAt − litAt ≈ 1.5、hits 0）。2026-10-19 ユーザー「触れないと消えない」
// 24 秒: done
// ============================================================
void AutoTestDashBomber::Run()
{
    char line[220];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    static float s_NextLog = 0.0f, s_LastHp = 0.0f, s_Lost = 0.0f, s_HpAtLit = -1.0f, s_LastEnemyHp = 0.0f, s_BlastAt = -1.0f, s_LitAt = -1.0f;
    static int s_Hits = 0;
    static bool s_LookLit = false, s_LookBlast = false, s_Exploded = false, s_Teleported = false;
    static const char* s_Phase = "A";

    auto spawnAt = [&](float dz, uint32_t kind)
    {
        const float groundY = m_Swarm.GetAIParams().groundY;
        float hp = 15.0f, speed = 3.5f;
        m_Mobs.KindStats(kind, hp, speed);
        const float x = tf.position.x, z = tf.position.z + dz;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), hp, speed, kind);
        snprintf(line, sizeof(line), "dashbomber %s start kind %u hp %.0f speed %.2f at %.0fm", s_Phase, kind, hp, speed, dz);
        AutoTestLog(line);
    };
    auto beginPhase = [&](const char* ph, float dz, uint32_t kind, bool casting)
    {
        s_Phase = ph;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = !casting;
        s_Hits = 0; s_Lost = 0.0f; s_HpAtLit = -1.0f; s_LastEnemyHp = 0.0f; s_BlastAt = -1.0f; s_LitAt = -1.0f;
        s_LookLit = s_LookBlast = s_Exploded = s_Teleported = false;
        s_NextLog = m_AutoTime + 0.25f;
        spawnAt(dz, kind);
    };
    auto result = [&]()
    {
        snprintf(line, sizeof(line), "dashbomber %s result hits %d lost %.0f litAt %.2f blastAt %.2f hpAtLit %.0f hpBeforeBlast %.0f",
            s_Phase, s_Hits, s_Lost, s_LitAt, s_BlastAt, s_HpAtLit, s_LastEnemyHp);
        AutoTestLog(line);
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
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(28.0f);
        cam.distance = 12.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        beginPhase("A", 14.0f, Swarm::kEnemyKindDashBomber, false);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0 || m_AutoStep >= 5) return;

    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f) { s_Lost += s_LastHp - hp.current; ++s_Hits; }
        s_LastHp = hp.current;
    }

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog += 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        if (m_Swarm.DebugReadEnemies(en, st, &ex))
        {
            int alive = 0;
            float fuse = 0.0f, dist = -1.0f, speed = 0.0f, ehp = 0.0f;
            for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead) continue;
                ++alive;
                if (dist >= 0.0f) continue;
                fuse = ex[i].fuse;
                const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                dist = std::sqrt(dx * dx + dz * dz);
                speed = std::sqrt(en[i].velocity.x * en[i].velocity.x + en[i].velocity.z * en[i].velocity.z);
                ehp = Swarm::HpFromFixed(en[i].hp);
            }
            if (alive > 0)
            {
                s_LastEnemyHp = ehp;
                if (fuse > 0.0f && s_LitAt < 0.0f) { s_LitAt = m_AutoTime; s_HpAtLit = ehp; }
                // D: 点火したらプレイヤーを 40m 後ろへ（追いつけない距離）。導火線だけで爆発するかを見る
                if (fuse > 0.0f && !s_Teleported && s_Phase[0] == 'D')
                {
                    s_Teleported = true;
                    // 20m 後ろ（8 + 20 = 28m、1.5 秒 × 12 m/s = 18m では届かない）。40m だと台地の中に落ちてカメラが岩の中だった
                    const float nx = tf.position.x, nz = tf.position.z - 20.0f;
                    tf.position = Vector3(nx, m_Grid.SampleHeight(nx, nz) + 1.0f, nz);
                    m_Camera.Camera().SnapToTarget();
                    snprintf(line, sizeof(line), "dashbomber D teleported t %.2f fuse %.2f", m_AutoTime, fuse);
                    AutoTestLog(line);
                }
            }
            else if (s_LitAt >= 0.0f && !s_Exploded) { s_Exploded = true; s_BlastAt = m_AutoTime; }
            snprintf(line, sizeof(line), "dashbomber %s t %.2f fuse %.2f dist %.2f speed %.2f hp %.0f alive %d hits %d lost %.0f",
                s_Phase, m_AutoTime, fuse, dist, speed, ehp, alive, s_Hits, s_Lost);
            AutoTestLog(line);
            // D は点火の 0.9 秒後（走って近づいた所。足元の警告の輪を見る）、他は 0.3 秒後
            if (!s_LookLit && s_LitAt >= 0.0f && m_AutoTime >= s_LitAt + (s_Phase[0] == 'D' ? 0.45f : 0.3f))
            {
                snprintf(line, sizeof(line), "dashbomber look %slit", s_Phase);
                AutoTestLog(line);
                s_LookLit = true;
            }
        }
    }
    if (s_Hits > 0 && s_BlastAt < 0.0f) s_BlastAt = m_AutoTime;
    if (!s_LookBlast && s_BlastAt >= 0.0f && m_AutoTime >= s_BlastAt + 0.1f)
    {
        snprintf(line, sizeof(line), "dashbomber look %sblast", s_Phase);
        AutoTestLog(line);
        s_LookBlast = true;
    }

    if (m_AutoStep == 1 && m_AutoTime >= 7.0f)
    {
        result();
        beginPhase("B", 14.0f, Swarm::kEnemyKindDashBomber, true);
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 13.0f)
    {
        result();
        beginPhase("C", 3.0f, Swarm::kEnemyKindBomber, true);
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 18.0f)
    {
        result();
        beginPhase("D", 8.0f, Swarm::kEnemyKindDashBomber, false);
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 24.0f)
    {
        result();
        AutoTestLog("dashbomber done");
        m_AutoStep = 5;
    }
}

REGISTER_BATTLE_AUTOTEST("dashbomber", AutoTestDashBomber)
