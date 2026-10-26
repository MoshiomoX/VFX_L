// ============================================================
// AutoTestBossMoves.cpp
// TEMP-TEST: VFXL_SPELL_LAB=1 + VFXL_BATTLE_AUTOTEST=bossmoves
// Boss の 5 つの技（2026-10-07）：平らな実験場で、止まった Boss に技を 1 つずつ出させ、
//   立ったまま受ける（当たるはず）→ 決まった避け方（衝撃波 = 跳ぶ、突進 = 横へ走る）で避ける（当たらないはず）を記録・撮影
// 2026-10-24：VFXL_BOSS_TYPE=sand を足すと砂の守護者の技（砂の槍 / 潜砂奇襲 / 流沙 / 召喚）を同じやり方で
//   砂の槍 = 横へ走る、潜砂 = 輪が出たら Boss から離れる、流沙 = Boss から離れる方へ走って抜けられるか
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Player/PlayerControlSystem.h"

namespace
{
    class AutoTestBossMoves final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };

    // 1 回分の試し：どの技を、プレイヤーを Boss から何 m 離して、どう避けるか
    //   Side = 突進・槍の向きと直角に走る（dodgeAt 秒から dodgeLen 秒）
    //   Away = Boss から離れる方へ走る（dodgeAt 秒から。潜砂は輪が出た瞬間から）
    enum class Dodge { None, Jump, Side, Away };
    struct Trial
    {
        BossMove move;
        float dist;
        Dodge dodge;
        float lookA, lookB;   // 技を始めてからの撮影の時刻
        float length;         // この試しの長さ（秒）
        const char* name;
        float dodgeAt = 0.6f, dodgeLen = 1.2f;
    };
    const Trial kTrials[] = {
        { BossMove::RockRain,  14.0f, Dodge::None, 0.7f, 1.3f, 3.0f, "rockrain" },
        { BossMove::Shockwave, 14.0f, Dodge::None, 0.5f, 1.5f, 3.5f, "shockwave" },
        { BossMove::Charge,    18.0f, Dodge::None, 0.7f, 1.3f, 3.0f, "charge" },
        { BossMove::Shockwave, 14.0f, Dodge::Jump, 1.9f, 2.0f, 3.5f, "shockwave-jump" },
        { BossMove::Charge,    18.0f, Dodge::Side, 0.9f, 1.4f, 3.0f, "charge-side" },
        { BossMove::Slam,      14.0f, Dodge::None, 0.9f, 1.4f, 3.0f, "slam" },
        { BossMove::Summon,    14.0f, Dodge::None, 1.0f, 2.2f, 3.5f, "summon" },
    };
    const Trial kSandTrials[] = {
        { BossMove::SandSpear, 12.0f, Dodge::None, 0.6f, 1.35f, 3.0f, "sandspear" },
        { BossMove::SandSpear, 12.0f, Dodge::Side, 0.6f, 1.35f, 3.0f, "sandspear-side", 0.35f, 1.0f },
        { BossMove::Burrow,    14.0f, Dodge::None, 1.2f, 2.6f, 4.8f, "burrow" },
        { BossMove::Burrow,    14.0f, Dodge::Away, 1.2f, 2.6f, 4.8f, "burrow-away", 99.0f, 1.3f },
        { BossMove::Quicksand,  7.0f, Dodge::None, 0.5f, 2.5f, 4.5f, "quicksand" },
        { BossMove::Quicksand,  7.0f, Dodge::Away, 0.5f, 2.5f, 4.5f, "quicksand-away", 0.9f, 3.2f },
        { BossMove::Summon,    14.0f, Dodge::None, 1.0f, 2.2f, 3.5f, "summon-sand" },
    };
    // 大サソリ（2026-10-26）：毒針は 12m（突っ込み 8m なので届く）、鋏は 4m（扇 5.5m）。横 / 後ろへ逃げる方も
    const Trial kScorpionTrials[] = {
        { BossMove::Sting,     10.0f, Dodge::None, 0.5f, 0.95f, 3.0f, "sting" },
        { BossMove::Sting,     10.0f, Dodge::Side, 0.5f, 0.95f, 3.0f, "sting-side", 0.3f, 1.0f },
        { BossMove::ClawSweep,  4.0f, Dodge::None, 0.6f, 0.9f, 2.5f, "claw" },
        { BossMove::ClawSweep,  4.0f, Dodge::Away, 0.6f, 0.9f, 2.5f, "claw-away", 0.2f, 1.0f },
        { BossMove::Burrow,    14.0f, Dodge::None, 1.2f, 2.6f, 4.8f, "burrow-scorpion" },
        { BossMove::Summon,    14.0f, Dodge::None, 1.0f, 2.2f, 3.5f, "summon-scorpion" },
    };
    // 墓守の騎士（2026-10-26）：双鎖は 10m（届く 14m）、掛かれば引き寄せてすぐ重撃（chainpull の後の 2 秒で輪が爆発）。横へ逃げる方も
    const Trial kKnightTrials[] = {
        { BossMove::ChainPull, 10.0f, Dodge::None, 0.7f, 1.3f, 4.5f, "chainpull" },
        { BossMove::ChainPull, 10.0f, Dodge::Side, 0.7f, 1.3f, 4.5f, "chainpull-side", 0.3f, 1.2f },
        { BossMove::Slam,      14.0f, Dodge::None, 0.9f, 1.4f, 3.0f, "slam-knight" },
        { BossMove::Shockwave, 14.0f, Dodge::Jump, 1.9f, 2.0f, 3.5f, "shockwave-jump-knight" },
        { BossMove::Summon,    14.0f, Dodge::None, 1.0f, 2.2f, 3.5f, "summon-knight" },
    };
    // 月下の狼人（2026-10-26）：飛びかかりは 12m（空中 0.6 秒、着地の輪 3m = 跳び始めてから横へ 1 秒走れば出る）、三連は 5m
    const Trial kWolfTrials[] = {
        { BossMove::Pounce,    12.0f, Dodge::None, 0.4f, 0.85f, 3.0f, "pounce" },
        { BossMove::Pounce,    12.0f, Dodge::Side, 0.4f, 0.85f, 3.0f, "pounce-side", 0.55f, 1.0f },
        { BossMove::ClawCombo,  5.0f, Dodge::None, 0.3f, 0.9f, 4.0f, "clawcombo" },
        { BossMove::ClawCombo,  5.0f, Dodge::Side, 0.3f, 0.9f, 4.0f, "clawcombo-side", 0.2f, 2.5f },
        { BossMove::Howl,      14.0f, Dodge::None, 0.8f, 1.5f, 3.5f, "howl" },
    };
}

// ============================================================
// 1 秒: 湧き停止・全消し・詠唱停止。Boss を止まったまま呼ぶ（RequestBoss(false)）。Boss の自分の技は切る（QueueMove だけ）
// Boss が現れたら試しを順に：プレイヤーを Boss から dist m の所へ置き、カメラを背中から Boss の方へ（20m / 50°）、
// 技を出させ、lookA / lookB 秒に `bossmoves look <名前>A/B`、終わりに `bossmoves <名前> hits <当たった数> bossMoved <m> ...`
//   衝撃波-跳ぶ：帯がプレイヤーの 2m 手前に来る時刻（溜め 0.9 秒 + (dist - 2) / 速さ）に跳ぶ
//   突進-横：溜めの途中（0.6 秒）から 1.2 秒、突進の向きと直角に走る
//   砂の試し：0.25 秒毎に `bossmoves sand <名前> t dist burrow phase sink quick r strength`
// 全部の後 `bossmoves done`
// ============================================================
void AutoTestBossMoves::Run()
{
    static int s_Trial = -1;
    static float s_Base = 0.0f;
    static uint32_t s_HitsBefore = 0;
    static uint32_t s_SummonedBefore = 0;
    static Vector3 s_BossBefore;
    static bool s_ShotA = false, s_ShotB = false, s_Jumped = false;
    static float s_AwayFrom = -1.0f;   // Away：走り出した時刻（-1 = まだ）
    static float s_NextSandLog = 0.0f;
    char line[240];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    const bool sand = (m_BossAttacks.Type() == BossType::SandGuardian);
    const bool scorpion = (m_BossAttacks.Type() == BossType::Scorpion);
    const bool knight = (m_BossAttacks.Type() == BossType::CryptKnight);
    const bool wolf = (m_BossAttacks.Type() == BossType::Werewolf);
    const Trial* trials = sand ? kSandTrials : scorpion ? kScorpionTrials : knight ? kKnightTrials : wolf ? kWolfTrials : kTrials;
    const int trialCount = sand ? (int)(sizeof(kSandTrials) / sizeof(kSandTrials[0]))
        : scorpion ? (int)(sizeof(kScorpionTrials) / sizeof(kScorpionTrials[0]))
        : knight ? (int)(sizeof(kKnightTrials) / sizeof(kKnightTrials[0]))
        : wolf ? (int)(sizeof(kWolfTrials) / sizeof(kWolfTrials[0])) : (int)(sizeof(kTrials) / sizeof(kTrials[0]));

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_BossAttacks.enabled = false;
        m_Stage.bossHp = knight ? 3000.0f : 1.0e6f;   // 騎士は試しの後で削り殺して復活を見る（範囲 150 × 0.2 秒 = 600/秒 → 5 秒）
        // 実験場でなく本番の面（VFXL_STAGE=2 の砂漠など）でも死なないように
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        hp.max = hp.current = 100000.0f;
        const bool ok = m_Stage.RequestBoss(false);
        snprintf(line, sizeof(line), "bossmoves start requested %d type %s", ok ? 1 : 0, BossTypeName(m_BossAttacks.Type()));
        AutoTestLog(line);
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_Stage.IsBossAlive() && m_Stage.BossPos().LengthSquared() > 0.01f)   // 位置のリードバックが届いてから
    {
        m_AutoStep = 2;
        s_Trial = -1;
        s_Base = m_AutoTime - 100.0f;   // すぐ最初の試しへ
    }
    // ---- 墓守の騎士の復活（m_AutoStep 10 = 削る → 11 = 倒れた（蘇るのを待つ）→ 12 = 蘇った後を削る → done）----
    if (m_AutoStep >= 10 && m_AutoStep <= 12)
    {
        static float s_NextChip = 0.0f;
        static bool s_Shot = false;
        const float t = m_AutoTime - s_Base;
        if (m_Stage.IsBossAlive() && m_AutoTime >= s_NextChip)
        {
            s_NextChip = m_AutoTime + 0.25f;
            Swarm::Area a;
            a.center = m_Stage.BossPos();
            a.radius = 3.0f;
            a.damage = 150.0f;
            a.tickInterval = 0.2f;
            a.timeLeft = 0.22f;
            m_Swarm.SpawnArea(a);
        }
        if (m_AutoStep == 10 && m_Stage.IsBossReviving())
        {
            snprintf(line, sizeof(line), "bossmoves knight fell t %.2f livesUsed %d", t, m_Stage.BossLivesUsed());
            AutoTestLog(line);
            AutoTestLog("bossmoves look knightfell");
            m_AutoStep = 11;
            s_Shot = false;
        }
        else if (m_AutoStep == 11 && m_Stage.IsBossAlive())
        {
            snprintf(line, sizeof(line), "bossmoves knight revived t %.2f hpRatio %.2f enraged %d tint %.2f", t, m_Stage.BossHpRatio(),
                m_BossAttacks.forceEnraged ? 1 : 0, m_BossAttacks.bodyTint.z);
            AutoTestLog(line);
            m_AutoStep = 12;
            s_Base = m_AutoTime;
        }
        else if (m_AutoStep == 12)
        {
            if (!s_Shot && t >= 0.8f) { s_Shot = true; AutoTestLog("bossmoves look knightrevived"); }
            if (m_Stage.IsBossDefeated())
            {
                snprintf(line, sizeof(line), "bossmoves knight defeated t %.2f livesUsed %d", t, m_Stage.BossLivesUsed());
                AutoTestLog(line);
                AutoTestLog("bossmoves done");
                m_AutoStep = 3;
            }
        }
        if (t > 40.0f) { AutoTestLog("bossmoves knight timeout"); AutoTestLog("bossmoves done"); m_AutoStep = 3; }
        return;
    }
    if (m_AutoStep != 2) return;

    const float t = m_AutoTime - s_Base;
    const Vector3 boss = m_Stage.BossPos();
    auto flatDist = [&]() { const float dx = tf.position.x - boss.x, dz = tf.position.z - boss.z; return std::sqrt(dx * dx + dz * dz); };

    // ---- 次の試しへ ----
    if (s_Trial < 0 || t >= trials[s_Trial].length)
    {
        if (s_Trial >= 0)
        {
            const Trial& tr = trials[s_Trial];
            const Vector3 d = boss - s_BossBefore;
            snprintf(line, sizeof(line), "bossmoves %s hits %u bossMoved %.1f hp %.0f summoned %u alive %u playerDist %.1f poison %.1f",
                tr.name, m_BossSlamHits - s_HitsBefore,
                std::sqrt(d.x * d.x + d.z * d.z), m_Registry.Get<HealthComponent>(m_Player).current,
                m_BossAttacks.summoned - s_SummonedBefore, m_Swarm.GetCounters().aliveEnemies, flatDist(), m_BossAttacks.PoisonLeft());
            AutoTestLog(line);
        }
        ++s_Trial;
        pcs.testMove = Vector2::Zero;
        if (s_Trial >= trialCount)
        {
            pcs.testInput = false;
            if (knight)
            {
                // 墓守の騎士の復活：Boss の所に傷の範囲を置き続けて倒す → 倒れた / 立ち上がった / 本当に倒した を記録
                m_BossAttacks.enabled = true;
                s_Base = m_AutoTime;
                m_AutoStep = 10;
                AutoTestLog("bossmoves knight chip start");
                return;
            }
            AutoTestLog("bossmoves done");
            m_AutoStep = 3;
            return;
        }
        const Trial& tr = trials[s_Trial];
        // プレイヤーを Boss から dist m（最初の試しは今居る方向、以降も同じ向き）
        Vector3 away = tf.position - boss;
        away.y = 0.0f;
        if (away.LengthSquared() < 1e-2f) away = Vector3(0.0f, 0.0f, 1.0f);
        away.Normalize();
        const Vector3 p = boss + away * tr.dist;
        tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        m_Registry.Get<PlayerStateComponent>(m_Player).invincibleTimer = 0.0f;
        auto& cam = m_Camera.Camera();
        // 背中から Boss の方へ（前 = (-sin yaw, cos yaw)）。砂の技は斜め横から（真後ろだと Boss が上の HP 条に隠れる）
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(away.x, -away.z)) + ((sand || scorpion) ? 40.0f : 0.0f));
        cam.SetPitch((sand || scorpion) ? 38.0f : 50.0f);
        cam.distance = scorpion ? 18.0f : sand ? 24.0f : 20.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        m_BossAttacks.QueueMove(tr.move);
        s_HitsBefore = m_BossSlamHits;
        s_SummonedBefore = m_BossAttacks.summoned;
        s_BossBefore = boss;
        s_ShotA = s_ShotB = s_Jumped = false;
        s_AwayFrom = -1.0f;
        s_NextSandLog = 0.0f;
        s_Base = m_AutoTime;
        snprintf(line, sizeof(line), "bossmoves %s start dist %.1f dodge %d", tr.name,
            std::sqrt((boss.x - p.x) * (boss.x - p.x) + (boss.z - p.z) * (boss.z - p.z)), (int)tr.dodge);
        AutoTestLog(line);
        return;
    }

    const Trial& tr = trials[s_Trial];
    if (!s_ShotA && t >= tr.lookA) { snprintf(line, sizeof(line), "bossmoves look %sA", tr.name); AutoTestLog(line); s_ShotA = true; }
    if (!s_ShotB && t >= tr.lookB) { snprintf(line, sizeof(line), "bossmoves look %sB", tr.name); AutoTestLog(line); s_ShotB = true; }

    // 砂の技の様子
    if (sand && t >= s_NextSandLog)
    {
        s_NextSandLog += 0.25f;
        const auto& q = m_BossAttacks.Quicksand();
        snprintf(line, sizeof(line), "bossmoves sand %s t %.2f dist %.2f burrow %d sink %.2f mound %u quick %d r %.1f strength %.2f",
            tr.name, t, flatDist(), m_BossAttacks.BurrowPhase(), m_Swarm.GetBomberParams().bossBurrow, m_BossMoundVfx,
            q.active ? 1 : 0, q.radius, q.strength);
        AutoTestLog(line);
    }

    // ---- 避け方 ----
    pcs.testJump = false;
    if (tr.dodge == Dodge::Jump && !s_Jumped)
    {
        const float jumpAt = m_BossAttacks.waveWindup + (tr.dist - 2.0f) / (std::max)(m_BossAttacks.waveSpeed, 1.0f);
        if (t >= jumpAt) { pcs.testJump = true; s_Jumped = true; AutoTestLog("bossmoves jump"); }
    }
    if (tr.dodge == Dodge::Side)
        pcs.testMove = (t >= tr.dodgeAt && t < tr.dodgeAt + tr.dodgeLen) ? Vector2(1.0f, 0.0f) : Vector2::Zero;   // カメラの右（= 突進の向きと直角）
    if (tr.dodge == Dodge::Away)
    {
        // 潜砂：止まって輪が出た瞬間から。他は dodgeAt から
        const bool start = (tr.move == BossMove::Burrow) ? (m_BossAttacks.BurrowPhase() == 2) : (t >= tr.dodgeAt);
        if (s_AwayFrom < 0.0f && start)
        {
            s_AwayFrom = t;
            snprintf(line, sizeof(line), "bossmoves %s run t %.2f dist %.2f", tr.name, t, flatDist());
            AutoTestLog(line);
        }
        pcs.testMove = (s_AwayFrom >= 0.0f && t < s_AwayFrom + tr.dodgeLen) ? Vector2(0.0f, -1.0f) : Vector2::Zero;   // カメラの後ろ = Boss から離れる
    }
}

REGISTER_BATTLE_AUTOTEST("bossmoves", AutoTestBossMoves)
