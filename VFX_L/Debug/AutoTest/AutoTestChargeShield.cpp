// ============================================================
// AutoTestChargeShield.cpp
// TEMP-TEST: VFXL_SPELL_LAB=1 + VFXL_BATTLE_AUTOTEST=chargeshield（平らな実験場で）
// 突撃兵・盾兵・アイスランス（2026-10-08）を順に確かめる。GPU の敵の池はリードバックで読む（Map で待つ。自動テスト専用）
//   A 突撃兵が当たる : 正面 +Z 15m に 1 体。プレイヤーは立ったまま。段階（溜め / 突進 / 息切れ）が変わる度に記録、
//                      溜めの途中で `chargeshield look windup`、終わったら受けたダメージ（GPU の累計の差）
//   B 突撃兵を避ける : 同じ形で、溜めが始まったらカメラの右へ 1.2 秒走る → 受けたダメージ 0 のはず
//   C 盾兵の装甲     : 毒 / 石弾 をそれぞれ、雑魚 3 体 → 盾兵 3 体（動かない、HP 10 万）に 5 秒撃って減った HP を比べる。
//                      盾兵 / 雑魚 = 毒 0.2 前後（3 → 0.6）、石弾 0.77 前後（22 → 17）のはず。途中で `chargeshield look shield`
//   D 凍結           : アイスランスだけで、歩いてくる雑魚 3 体（HP 10 万）。0.25 秒毎に凍っている数・再凍結待ちの数・速さ、
//                      凍った敵が居る時に `chargeshield look ice`
//   E 見た目         : 突撃兵 5・盾兵 5 を歩かせて `chargeshield look crowd`、done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestChargeShield final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };

    constexpr float kTargetHp = 100000.0f;
}

void AutoTestChargeShield::Run()
{
    char line[256];
    if (!m_Registry.Has<TransformComponent>(m_Player) || !m_Registry.Has<BackpackComponent>(m_Player)) return;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    const float gy = m_Swarm.GetAIParams().groundY;
    const Swarm::BomberCB& bomb = m_Swarm.GetBomberParams();

    static float s_T0 = 0.0f, s_Dmg0 = 0.0f, s_NextPoll = 0.0f, s_RunT0 = 0.0f;
    static int s_LastPhase = -1, s_Run = 0;
    static bool s_Looked = false, s_Dodged = false;
    static double s_Hp0 = 0.0, s_Lost[4] = {};

    auto spawnAt = [&](float dx, float dz, uint32_t kind, float hp, float speed)
    {
        const float x = tf.position.x + dx, z = tf.position.z + dz;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), hp, speed, kind);
    };
    auto kindSpeed = [&](uint32_t kind) { float hp = 15.0f, sp = 3.5f; m_Mobs.KindStats(kind, hp, sp); return sp; };
    auto playerDamage = [&]() { return Swarm::HpFromFixed(m_Swarm.GetCounters().playerDamage); };
    auto phaseOf = [&](float fuse) -> int
    {
        const float w = bomb.chargerWindup;
        const float d = w + bomb.chargerDashDist / (std::max)(bomb.chargerDashSpeed, 0.1f);
        return (fuse <= 0.0f) ? 0 : (fuse <= w) ? 1 : (fuse <= d) ? 2 : 3;
    };
    auto useSpell = [&](ItemID id)
    {
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, id, 4, 4, 0);
        bp.dirty = true;
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
    };
    auto readPool = [&](std::vector<Swarm::Enemy>& en, std::vector<uint32_t>& st, std::vector<Swarm::EnemyExtra>& ex,
                        std::vector<Vector4>& status) -> bool
    {
        return m_Swarm.DebugReadEnemies(en, st, &ex, &status) && ex.size() == en.size() && status.size() == en.size();
    };
    auto clearField = [&]()
    {
        m_Swarm.KillAll();
        m_Swarm.ClearProjectiles();
        m_Swarm.ClearAreas();
    };

    // ---- 準備 ----
    if (m_AutoStep == 0)
    {
        if (m_AutoTime < 1.0f) return;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        clearField();
        ClearBackpackItems(bp);
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(40.0f);
        cam.distance = 16.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("chargeshield start");
        m_AutoStep = 10;
        // VFXL_CS_LOOK=1：近景（F）だけ撮る
        if (GetEnvironmentVariableA("VFXL_CS_LOOK", nullptr, 0) > 0)
        {
            s_T0 = m_AutoTime - 3.0f;
            m_AutoStep = 52;
        }
    }

    // ---- A / B：突撃兵 ----
    if (m_AutoStep == 10 || m_AutoStep == 20)
    {
        const bool dodge = (m_AutoStep == 20);
        clearField();
        pcs.testMove = Vector2::Zero;
        spawnAt(0.0f, 15.0f, Swarm::kEnemyKindCharger, kTargetHp, kindSpeed(Swarm::kEnemyKindCharger));
        s_T0 = m_AutoTime;
        s_Dmg0 = -1.0f;
        s_NextPoll = m_AutoTime + 0.3f;
        s_LastPhase = -1;
        s_Looked = s_Dodged = false;
        snprintf(line, sizeof(line), "chargeshield %s start windup %.2f dashSpeed %.1f dashDist %.1f trigger %.0f-%.0f",
            dodge ? "B" : "A", bomb.chargerWindup, bomb.chargerDashSpeed, bomb.chargerDashDist, bomb.chargerMinDist, bomb.chargerMaxDist);
        AutoTestLog(line);
        ++m_AutoStep;
    }
    if (m_AutoStep == 11 || m_AutoStep == 21)
    {
        const bool dodge = (m_AutoStep == 21);
        if (s_Dmg0 < 0.0f && m_AutoTime >= s_T0 + 0.2f) s_Dmg0 = playerDamage();   // リードバックが追いつくまで少し待つ
        if (m_AutoTime >= s_NextPoll)
        {
            s_NextPoll = m_AutoTime + 0.1f;
            std::vector<Swarm::Enemy> en; std::vector<uint32_t> st; std::vector<Swarm::EnemyExtra> ex; std::vector<Vector4> status;
            if (readPool(en, st, ex, status))
            {
                for (size_t i = 0; i < en.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead || ex[i].kind != Swarm::kEnemyKindCharger) continue;
                    const int ph = phaseOf(ex[i].fuse);
                    const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                    const float dist = std::sqrt(dx * dx + dz * dz);
                    const float speed = std::sqrt(en[i].velocity.x * en[i].velocity.x + en[i].velocity.z * en[i].velocity.z);
                    if (ph != s_LastPhase)
                    {
                        snprintf(line, sizeof(line), "chargeshield %s phase %d t %.2f dist %.2f speed %.2f fuse %.2f dmg %.1f",
                            dodge ? "B" : "A", ph, m_AutoTime - s_T0, dist, speed, ex[i].fuse, playerDamage() - (std::max)(s_Dmg0, 0.0f));
                        AutoTestLog(line);
                        // 溜めが始まったら（B）カメラの右（= 突進と直角）へ逃げる
                        if (dodge && ph == 1 && !s_Dodged)
                        {
                            pcs.testMove = Vector2(1.0f, 0.0f);
                            s_RunT0 = m_AutoTime;
                            s_Dodged = true;
                        }
                        // 息切れ（3）か追いかけ（0）に戻ったら結果
                        if (s_LastPhase >= 2 && (ph == 3 || ph == 0))
                        {
                            snprintf(line, sizeof(line), "chargeshield %s result dmgTaken %.1f (melee base %.1f x dash %.2f)",
                                dodge ? "B" : "A", playerDamage() - s_Dmg0, m_Swarm.GetAIParams().contactDamage, bomb.chargerDashDamageMul);
                            AutoTestLog(line);
                            m_AutoStep = dodge ? 30 : 20;
                        }
                        s_LastPhase = ph;
                    }
                    if (!dodge && ph == 1 && !s_Looked && ex[i].fuse > bomb.chargerWindup * 0.6f)
                    {
                        AutoTestLog("chargeshield look windup");
                        s_Looked = true;
                    }
                    break;
                }
            }
        }
        if (s_Dodged && m_AutoTime >= s_RunT0 + 1.2f) pcs.testMove = Vector2::Zero;
        if ((m_AutoStep == 11 || m_AutoStep == 21) && m_AutoTime >= s_T0 + 10.0f)
        {
            AutoTestLog(dodge ? "chargeshield B timeout" : "chargeshield A timeout");
            m_AutoStep = dodge ? 30 : 20;
        }
    }

    // ---- C：盾兵の装甲（毒 → 石弾、それぞれ雑魚の的 → 盾兵の的）----
    if (m_AutoStep == 30)
    {
        pcs.testMove = Vector2::Zero;
        clearField();
        s_Run = 0;
        m_AutoStep = 31;
    }
    if (m_AutoStep == 31)
    {
        // s_Run: 0 毒 × 雑魚 / 1 毒 × 盾兵 / 2 石弾 × 雑魚 / 3 石弾 × 盾兵
        clearField();
        useSpell(s_Run < 2 ? ItemID::Poison : ItemID::StoneShot);
        const uint32_t kind = (s_Run % 2) ? Swarm::kEnemyKindShield : Swarm::kEnemyKindMob;
        for (int k = 0; k < 3; ++k)
            spawnAt(((float)k - 1.0f) * 1.6f, 9.0f, kind, kTargetHp, 0.0f);
        s_T0 = m_AutoTime;
        s_Hp0 = -1.0;
        s_Looked = false;
        m_AutoStep = 32;
    }
    if (m_AutoStep == 32)
    {
        auto sumHp = [&]() -> double
        {
            std::vector<Swarm::Enemy> en; std::vector<uint32_t> st;
            if (!m_Swarm.DebugReadEnemies(en, st)) return -1.0;
            double s = 0.0;
            for (size_t i = 0; i < en.size(); ++i) if (st[i] != Swarm::kStateDead) s += Swarm::HpFromFixed(en[i].hp);
            return s;
        };
        if (s_Hp0 < 0.0 && m_AutoTime >= s_T0 + 1.5f) s_Hp0 = sumHp();   // 最初の弾 / 沼が届いてから測る
        if (s_Run == 1 && !s_Looked && m_AutoTime >= s_T0 + 3.0f) { AutoTestLog("chargeshield look shield"); s_Looked = true; }
        if (s_Hp0 >= 0.0 && m_AutoTime >= s_T0 + 6.5f)
        {
            s_Lost[s_Run] = s_Hp0 - sumHp();
            static const char* kNames[4] = { "poison mob", "poison shield", "stone mob", "stone shield" };
            snprintf(line, sizeof(line), "chargeshield C %s lost %.1f in 5s", kNames[s_Run], s_Lost[s_Run]);
            AutoTestLog(line);
            if (++s_Run < 4) m_AutoStep = 31;
            else
            {
                snprintf(line, sizeof(line), "chargeshield C ratio poison %.2f stone %.2f (armor %.1f minFrac %.2f)",
                    s_Lost[0] > 0.0 ? s_Lost[1] / s_Lost[0] : 0.0, s_Lost[2] > 0.0 ? s_Lost[3] / s_Lost[2] : 0.0,
                    bomb.shieldArmor, bomb.shieldMinFrac);
                AutoTestLog(line);
                m_AutoStep = 40;
            }
        }
    }

    // ---- D：凍結 ----
    if (m_AutoStep == 40)
    {
        clearField();
        useSpell(ItemID::IceLance);
        for (int k = 0; k < 3; ++k)
            spawnAt(((float)k - 1.0f) * 2.0f, 14.0f, Swarm::kEnemyKindMob, kTargetHp, 3.5f);
        s_T0 = m_AutoTime;
        s_NextPoll = m_AutoTime + 0.5f;
        s_Looked = false;
        m_AutoStep = 41;
    }
    if (m_AutoStep == 41)
    {
        if (m_AutoTime >= s_NextPoll)
        {
            s_NextPoll = m_AutoTime + 0.25f;
            std::vector<Swarm::Enemy> en; std::vector<uint32_t> st; std::vector<Swarm::EnemyExtra> ex; std::vector<Vector4> status;
            if (readPool(en, st, ex, status))
            {
                int alive = 0, frozen = 0, immune = 0;
                float frozenSpeed = 0.0f, nearest = 1e9f;
                for (size_t i = 0; i < en.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    ++alive;
                    const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
                    nearest = (std::min)(nearest, std::sqrt(dx * dx + dz * dz));
                    if (status[i].z > 0.0f)
                    {
                        ++frozen;
                        frozenSpeed = (std::max)(frozenSpeed, std::sqrt(en[i].velocity.x * en[i].velocity.x + en[i].velocity.z * en[i].velocity.z));
                    }
                    else if (status[i].w > 0.0f) ++immune;
                }
                snprintf(line, sizeof(line), "chargeshield D t %.2f alive %d frozen %d immune %d frozenMaxSpeed %.2f nearest %.1f dmg %.1f",
                    m_AutoTime - s_T0, alive, frozen, immune, frozenSpeed, nearest, playerDamage());
                AutoTestLog(line);
                if (frozen > 0 && !s_Looked && m_AutoTime >= s_T0 + 1.5f) { AutoTestLog("chargeshield look ice"); s_Looked = true; }
            }
        }
        if (m_AutoTime >= s_T0 + 7.0f) m_AutoStep = 50;
    }

    // ---- E：見た目（突撃兵・盾兵が歩いてくる）----
    if (m_AutoStep == 50)
    {
        clearField();
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        for (int k = 0; k < 10; ++k)
        {
            const uint32_t kind = (k % 2) ? Swarm::kEnemyKindShield : Swarm::kEnemyKindCharger;
            spawnAt(((float)k - 4.5f) * 1.5f, 13.0f + (float)(k % 3), kind, kTargetHp, kindSpeed(kind));
        }
        s_T0 = m_AutoTime;
        m_AutoStep = 51;
    }
    if (m_AutoStep == 51 && m_AutoTime >= s_T0 + 1.6f) { AutoTestLog("chargeshield look crowd"); m_AutoStep = 52; }

    // ---- F：近景（カメラを 180 度回して、こちらを向いた止まった的を 7m から）。盾の形・凍った姿 ----
    // 止まった敵は向き 0（+Z）のまま = カメラ（-Z を見る）の方を向く
    if (m_AutoStep == 52 && m_AutoTime >= s_T0 + 3.0f)
    {
        clearField();
        spawnAt(-1.4f, -4.5f, Swarm::kEnemyKindShield, kTargetHp, 0.0f);
        spawnAt(0.0f, -5.0f, Swarm::kEnemyKindCharger, kTargetHp, 0.0f);
        spawnAt(1.4f, -4.5f, Swarm::kEnemyKindMob, kTargetHp, 0.0f);
        auto& cam = m_Camera.Camera();
        cam.SetYaw(180.0f);
        cam.SetPitch(12.0f);
        cam.distance = 5.0f;
        cam.SnapToTarget();
        s_T0 = m_AutoTime;
        m_AutoStep = 53;
    }
    if (m_AutoStep == 53 && m_AutoTime >= s_T0 + 1.0f)
    {
        AutoTestLog("chargeshield look close");
        useSpell(ItemID::IceLance);   // 3 体とも凍らせる（1 発 1 体、0.75 秒毎）
        s_NextPoll = m_AutoTime;
        m_AutoStep = 54;
    }
    // 凍った的が出たら（氷が伸び切った後、溶け始める前）撮る。1 発 1 体、一番近い的ばかり狙うので 1 体だけのことが多い
    if (m_AutoStep == 54 && m_AutoTime >= s_NextPoll)
    {
        s_NextPoll = m_AutoTime + 0.1f;
        std::vector<Swarm::Enemy> en; std::vector<uint32_t> st; std::vector<Swarm::EnemyExtra> ex; std::vector<Vector4> status;
        int frozen = 0;
        uint32_t kind = 0;
        float left = 0.0f;
        if (readPool(en, st, ex, status))
            for (size_t i = 0; i < en.size(); ++i)
                if (st[i] != Swarm::kStateDead && status[i].z > 0.0f) { ++frozen; kind = ex[i].kind; left = status[i].z; }
        if ((frozen > 0 && left <= 0.9f) || m_AutoTime >= s_T0 + 3.5f)
        {
            snprintf(line, sizeof(line), "chargeshield F t %.2f frozen %d kind %u left %.2f", m_AutoTime - s_T0, frozen, kind, left);
            AutoTestLog(line);
            AutoTestLog("chargeshield look closeice");
            s_T0 = m_AutoTime - 3.3f;   // 0.7 秒後に終わる
            m_AutoStep = 55;
        }
    }
    if (m_AutoStep == 55 && m_AutoTime >= s_T0 + 4.0f)
    {
        clearField();
        m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        pcs.testInput = false;
        AutoTestLog("chargeshield done");
        m_AutoStep = 56;
    }
}

REGISTER_BATTLE_AUTOTEST("chargeshield", AutoTestChargeShield)
