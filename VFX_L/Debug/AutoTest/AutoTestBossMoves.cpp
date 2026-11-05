// ============================================================
// AutoTestBossMoves.cpp
// TEMP-TEST: VFXL_SPELL_LAB=1 + VFXL_BATTLE_AUTOTEST=bossmoves
// Boss の 5 つの技（2026-10-07）：平らな実験場で、止まった Boss に技を 1 つずつ出させ、
//   立ったまま受ける（当たるはず）→ 決まった避け方（衝撃波 = 跳ぶ、突進 = 横へ走る）で避ける（当たらないはず）を記録・撮影
// 2026-10-24：VFXL_BOSS_TYPE=sand を足すと砂の守護者の技（砂の槍 / 潜砂奇襲 / 流沙 / 召喚）を同じやり方で
//   砂の槍 = 横へ走る、潜砂 = 輪が出たら Boss から離れる、流沙 = Boss から離れる方へ走って抜けられるか
// 2026-10-29：VFXL_STAGE=3 VFXL_BOSS_TYPE=king で骸の王（呪いの玉 / 呪いの床 / 大起尸。骸骨が湧くので大起尸は最後。影へ消えるは使わなくなった）
// 2026-10-29：VFXL_STAGE=2 VFXL_BOSS_TYPE=djinn で砂嵐の魔神（一段目の 3 つ → Boss を削って咆哮で二段目へ → 二段目の技）
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
    //   Gap = 砂嵐召喚の砂旋の間（Boss から離れる向きに一番近い隙間）を抜けて輪の外へ走る（dodgeAt 秒から）
    enum class Dodge { None, Jump, Side, Away, Gap, Weave };   // Weave = 石羽の散弾：右へ 1 歩（隙間へ）→ dodgeAt + dodgeLen で左へ 1 歩（戻る）
    struct Trial
    {
        BossMove move;
        float dist;
        Dodge dodge;
        float lookA, lookB;   // 技を始めてからの撮影の時刻
        float length;         // この試しの長さ（秒）
        const char* name;
        float dodgeAt = 0.6f, dodgeLen = 1.2f;
        bool  enraged = false;   // この試しの間だけ狂暴として扱う（BossAttacks::testEnraged。雄鹿の三連突進・5 本の雷撃）
        bool  chip = false;      // 砂嵐の魔神：二段目に入るまで Boss の所に傷の範囲を置き続ける（狂暴の咆哮 → 降りる）
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
        { BossMove::ClawSweep,  4.0f, Dodge::Away, 0.6f, 0.9f, 2.5f, "claw-away", 0.0f, 1.0f },   // 10-29 扇 6.6m：予告が出た瞬間に走る（0.2 秒遅れだと届く）
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
    // 月下の狼人（2026-10-26）：飛びかかりは 12m（10-29 から空中 0.75 秒・着地の輪 3.6m = 跳んだ瞬間から横へ走れば出る）、三連は 5m
    const Trial kWolfTrials[] = {
        { BossMove::Pounce,    12.0f, Dodge::None, 0.4f, 0.85f, 3.0f, "pounce" },
        { BossMove::Pounce,    12.0f, Dodge::Side, 0.4f, 0.85f, 3.0f, "pounce-side", 0.55f, 1.0f },
        { BossMove::ClawCombo,  5.0f, Dodge::None, 0.3f, 0.9f, 4.0f, "clawcombo" },
        { BossMove::ClawCombo,  5.0f, Dodge::Side, 0.3f, 0.9f, 4.0f, "clawcombo-side", 0.0f, 3.0f },   // 10-29 当たり 2.2m：予告が出た瞬間から、帯毎に横へ
        { BossMove::Howl,      14.0f, Dodge::None, 0.8f, 1.5f, 3.5f, "howl" },
    };
    // 雷鳴の雄鹿（2026-10-29）：突進（道筋に雷）、振り上げは目の前 3.5m（扇 5m / ±45°）、雷撃は 11m（3 本 ±18°）。
    // 横へ = 予告が出た瞬間から走る。最後の 2 つは狂暴（雷撃 5 本、突進三連）
    const Trial kStagTrials[] = {
        { BossMove::Charge,     18.0f, Dodge::None, 0.7f, 1.3f, 3.0f, "charge-stag" },
        { BossMove::Charge,     18.0f, Dodge::Side, 0.9f, 1.4f, 3.0f, "charge-stag-side" },
        { BossMove::AntlerToss,  3.5f, Dodge::None, 0.6f, 0.95f, 2.5f, "antlertoss" },
        { BossMove::AntlerToss,  3.5f, Dodge::Side, 0.6f, 0.95f, 2.5f, "antlertoss-side", 0.0f, 1.0f },
        { BossMove::AntlerBolt, 11.0f, Dodge::None, 0.6f, 0.95f, 2.5f, "antlerbolt" },
        { BossMove::AntlerBolt, 11.0f, Dodge::Side, 0.6f, 0.95f, 2.5f, "antlerbolt-side", 0.0f, 1.2f },
        { BossMove::AntlerBolt, 11.0f, Dodge::None, 0.6f, 0.95f, 2.5f, "antlerbolt-enraged", 0.6f, 1.2f, true },
        { BossMove::Charge,     18.0f, Dodge::None, 2.6f, 4.3f, 6.5f, "charge-enraged", 0.6f, 1.2f, true },
    };
    // 骸の王（2026-10-29）
    const Trial kKingTrials[] = {
        { BossMove::CurseOrbs,  11.0f, Dodge::None, 1.2f, 2.6f, 5.5f, "orbs" },
        { BossMove::CurseOrbs,  11.0f, Dodge::Away, 1.2f, 2.6f, 7.5f, "orbs-away", 0.3f, 7.0f },
        { BossMove::CurseFloor, 10.0f, Dodge::None, 0.6f, 3.0f, 7.5f, "curse" },
        { BossMove::CurseFloor, 10.0f, Dodge::Away, 0.6f, 3.0f, 7.5f, "curse-away", 0.0f, 4.0f },
        { BossMove::RaiseDead,  12.0f, Dodge::None, 1.0f, 2.2f, 4.0f, "raise" },
    };    // 砂嵐の魔神（2026-10-29）：一段目（浮いている）の 3 つ → 削って咆哮で二段目 → 二段目の技
    const Trial kDjinnTrials[] = {
        { BossMove::DustDevils, 10.0f, Dodge::None, 1.6f, 4.5f, 7.2f, "devils" },
        { BossMove::DustDevils, 10.0f, Dodge::Gap,  1.6f, 3.0f, 7.2f, "devils-gap", 0.2f, 2.6f },
        { BossMove::DjinnBreath, 8.0f, Dodge::None, 0.7f, 1.8f, 3.8f, "breath" },
        { BossMove::DjinnBreath, 8.0f, Dodge::Away, 0.7f, 1.8f, 3.8f, "breath-away", 0.2f, 2.6f },
        { BossMove::DjinnShift, 10.0f, Dodge::None, 0.9f, 2.4f, 5.5f, "shift" },
        { BossMove::None,       10.0f, Dodge::None, 3.0f, 5.0f, 8.0f, "roar", 0.6f, 1.2f, false, true },
        { BossMove::SandFist,    4.0f, Dodge::None, 0.5f, 0.85f, 2.5f, "fist" },
        { BossMove::SandFist,    4.0f, Dodge::Away, 0.5f, 0.85f, 2.5f, "fist-away", 0.0f, 1.0f },
        { BossMove::DustDevils, 10.0f, Dodge::None, 1.6f, 4.5f, 7.2f, "devils-p2" },
        { BossMove::Shockwave,  14.0f, Dodge::Jump, 1.9f, 2.0f, 3.5f, "shockwave-jump-djinn" },
        { BossMove::Quicksand,   7.0f, Dodge::None, 0.5f, 2.5f, 4.5f, "quicksand-djinn" },
    };
    // 石の守護獣（2026-10-29）：一段目（飛ぶ）→ "roar" で半分まで削って二段目（降りる）
    const Trial kBeastTrials[] = {
        { BossMove::BeastDive,     14.0f, Dodge::None, 0.6f, 1.25f, 3.5f, "dive" },
        // すぐ後に置く：天からの圧殺は Boss を動かすので、その後だと横へ走る先が壁になる（2026-11-01）
        { BossMove::BeastDive,     14.0f, Dodge::Side, 0.6f, 1.25f, 3.5f, "dive-side", 0.3f, 1.2f },
        { BossMove::SkyCrush,      10.0f, Dodge::None, 1.5f, 3.3f, 6.0f, "crush" },
        { BossMove::SkyCrush,      10.0f, Dodge::Away, 1.5f, 3.3f, 6.0f, "crush-away", 2.3f, 0.9f },
        { BossMove::FeatherShards, 10.0f, Dodge::None, 0.5f, 1.25f, 3.4f, "shards" },
        { BossMove::FeatherShards, 10.0f, Dodge::Weave, 0.5f, 1.25f, 3.4f, "shards-weave", 0.9f, 0.5f },
        { BossMove::PetrifyBreath,  8.0f, Dodge::None, 0.8f, 2.4f, 4.6f, "breath" },
        { BossMove::PetrifyBreath,  8.0f, Dodge::Away, 0.8f, 2.0f, 4.0f, "breath-away", 0.2f, 2.6f },
        { BossMove::WingGust,       8.0f, Dodge::None, 0.6f, 1.15f, 3.2f, "gust" },
        { BossMove::WingGust,       8.0f, Dodge::Side, 0.6f, 1.15f, 3.2f, "gust-side", 0.0f, 1.3f },
        { BossMove::None,          10.0f, Dodge::None, 3.0f, 5.0f, 8.0f, "roar", 0.6f, 1.2f, false, true },
        { BossMove::TailSweep,      5.0f, Dodge::None, 0.5f, 0.95f, 2.4f, "tail" },
        { BossMove::TailSweep,      5.0f, Dodge::Jump, 0.5f, 0.95f, 2.4f, "tail-jump" },
        { BossMove::PetrifyBreath,  8.0f, Dodge::None, 0.8f, 2.4f, 4.6f, "breath-p2" },
        { BossMove::Charge,        18.0f, Dodge::None, 0.7f, 1.3f, 3.0f, "charge-beast" },
        { BossMove::RockRain,      14.0f, Dodge::None, 0.7f, 1.3f, 3.0f, "rockrain-beast" },
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
    static Vector3 s_SideDir;          // 爪の三連の横：今の帯と直角で線から離れる向き（世界。帯が出る度に取り直す）
    char line[240];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    const bool sand = (m_BossAttacks.Type() == BossType::SandGuardian);
    const bool scorpion = (m_BossAttacks.Type() == BossType::Scorpion);
    const bool knight = (m_BossAttacks.Type() == BossType::CryptKnight);
    const bool wolf = (m_BossAttacks.Type() == BossType::Werewolf);
    const bool stag = (m_BossAttacks.Type() == BossType::Stag);
    const bool djinn = (m_BossAttacks.Type() == BossType::Djinn);
    const bool king = (m_BossAttacks.Type() == BossType::BoneKing);
    const bool beast = (m_BossAttacks.Type() == BossType::StoneBeast);
    const Trial* trials = sand ? kSandTrials : scorpion ? kScorpionTrials : knight ? kKnightTrials : wolf ? kWolfTrials
        : stag ? kStagTrials : djinn ? kDjinnTrials : king ? kKingTrials : beast ? kBeastTrials : kTrials;
    const int trialCount = sand ? (int)(sizeof(kSandTrials) / sizeof(kSandTrials[0]))
        : scorpion ? (int)(sizeof(kScorpionTrials) / sizeof(kScorpionTrials[0]))
        : knight ? (int)(sizeof(kKnightTrials) / sizeof(kKnightTrials[0]))
        : wolf ? (int)(sizeof(kWolfTrials) / sizeof(kWolfTrials[0]))
        : stag ? (int)(sizeof(kStagTrials) / sizeof(kStagTrials[0]))
        : djinn ? (int)(sizeof(kDjinnTrials) / sizeof(kDjinnTrials[0]))
        : king ? (int)(sizeof(kKingTrials) / sizeof(kKingTrials[0]))
        : beast ? (int)(sizeof(kBeastTrials) / sizeof(kBeastTrials[0])) : (int)(sizeof(kTrials) / sizeof(kTrials[0]));

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_BossAttacks.enabled = false;
        // 騎士は試しの後で削り殺して復活を見る（範囲 150 × 0.2 秒 = 600/秒 → 5 秒）、魔神は "roar" の試しで半分まで削って二段目へ
        m_Stage.bossHp = (knight || djinn || beast) ? 3000.0f : 1.0e6f;
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
    // ---- 石の守護獣：息なしで石化させ、寄って撮る（m_AutoStep 13、2026-11-05）----
    if (m_AutoStep == 13 && m_Registry.Has<SkinnedAnimComponent>(m_Player))
    {
        static int s_Phase = 0;
        static float s_AnimAt = 0.0f;
        const float t = m_AutoTime - s_Base;
        const auto& a = m_Registry.Get<SkinnedAnimComponent>(m_Player);
        if (s_Phase == 0)
        {
            // Boss から 12m 離して（体の殴りが届かない）、正面寄りの斜めから近く低く
            const Vector3 boss = m_Stage.BossPos();
            Vector3 away = tf.position - boss;
            away.y = 0.0f;
            if (away.LengthSquared() < 1e-2f) away = Vector3(0.0f, 0.0f, 1.0f);
            away.Normalize();
            const Vector3 p = boss + away * 12.0f;
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            pcs.testInput = true;
            pcs.testMove = Vector2::Zero;
            auto& cam = m_Camera.Camera();
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(away.x, -away.z)) + 150.0f);
            cam.SetPitch(12.0f);
            cam.distance = 4.5f;
            cam.avoidOcclusion = false;
            cam.SnapToTarget();
            s_Phase = 1;
        }
        else if (s_Phase == 1 && t >= 0.8f)   // 着地して立ち止まってから
        {
            m_BossAttacks.TestSetBeastStone(1.6f);
            s_Phase = 2;
        }
        else if (s_Phase == 2 && t >= 1.4f)
        {
            snprintf(line, sizeof(line), "bossmoves beast stoneclose hitFlash %.2f color %.2f,%.2f,%.2f animTime %.3f stoneLeft %.2f",
                a.hitFlash, a.hitFlashColor.x, a.hitFlashColor.y, a.hitFlashColor.z, a.base.time, m_BossAttacks.BeastStoneLeft());
            AutoTestLog(line);
            AutoTestLog("bossmoves look stoneclose");
            s_AnimAt = a.base.time;
            s_Phase = 3;
        }
        else if (s_Phase == 3 && t >= 2.0f)
        {
            snprintf(line, sizeof(line), "bossmoves beast stoneclose2 animTime %.3f (was %.3f) stoneLeft %.2f",
                a.base.time, s_AnimAt, m_BossAttacks.BeastStoneLeft());
            AutoTestLog(line);
            s_Phase = 4;
        }
        else if (s_Phase == 4 && t >= 3.4f)   // 石化が解けた後（1.6 秒 + 欠片が散るまで）
        {
            snprintf(line, sizeof(line), "bossmoves beast stoneafter hitFlash %.2f animTime %.3f stoneLeft %.2f petrify %.2f",
                a.hitFlash, a.base.time, m_BossAttacks.BeastStoneLeft(), m_BossAttacks.BeastPetrify());
            AutoTestLog(line);
            AutoTestLog("bossmoves look stoneafter");
            s_Phase = 5;
        }
        else if (s_Phase == 5 && t >= 3.8f)
        {
            pcs.testInput = false;
            AutoTestLog("bossmoves done");
            m_AutoStep = 3;
        }
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
            snprintf(line, sizeof(line), "bossmoves %s hits %u bossMoved %.1f hp %.0f summoned %u alive %u playerDist %.1f poison %.1f phase %d hover %.2f",
                tr.name, m_BossSlamHits - s_HitsBefore,
                std::sqrt(d.x * d.x + d.z * d.z), m_Registry.Get<HealthComponent>(m_Player).current,
                m_BossAttacks.summoned - s_SummonedBefore, m_Swarm.GetCounters().aliveEnemies, flatDist(), m_BossAttacks.PoisonLeft(),
                (m_BossAttacks.DjinnPhase2() || m_BossAttacks.BeastPhase2()) ? 2 : 1, m_Swarm.GetBomberParams().bossHover);
            AutoTestLog(line);
        }
        ++s_Trial;
        pcs.testMove = Vector2::Zero;
        m_BossAttacks.testEnraged = false;
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
            if (beast)
            {
                // 石化の寄りの絵（2026-11-05）：息の煙が無い所で石化だけ起こす（m_AutoStep 13）
                s_Base = m_AutoTime;
                m_AutoStep = 13;
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
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(away.x, -away.z)) + ((sand || scorpion || djinn || king) ? 40.0f : 0.0f));
        cam.SetPitch((sand || scorpion) ? 38.0f : (djinn || king) ? 34.0f : beast ? 36.0f : 50.0f);
        cam.distance = scorpion ? 18.0f : (sand || djinn || king) ? 24.0f : beast ? 27.0f : 20.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        m_BossAttacks.testEnraged = tr.enraged;
        if (tr.move != BossMove::None) m_BossAttacks.QueueMove(tr.move);
        s_HitsBefore = m_BossSlamHits;
        s_SummonedBefore = m_BossAttacks.summoned;
        s_BossBefore = boss;
        s_ShotA = s_ShotB = s_Jumped = false;
        s_AwayFrom = -1.0f;
        s_SideDir = Vector3::Zero;
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

    // 骸の王の様子（0.25 秒毎）
    if (king && t >= s_NextSandLog)
    {
        s_NextSandLog += 0.25f;
        const auto& bp = m_Swarm.GetBomberParams();
        const KingCurse& c = m_BossAttacks.KingCurseField();
        snprintf(line, sizeof(line), "bossmoves king %s t %.2f dist %.2f fly %.0f keep %.0f-%.0f sink %.2f orbs %d curse %d r %.1f raised %u alive %u pose %.2f",
            tr.name, t, flatDist(), bp.bossFly, bp.bossKeepMin, bp.bossKeepMax, bp.bossBurrow, (int)m_BossAttacks.KingOrbs().size(),
            c.active ? 1 : 0, c.radius, m_KingRaised, m_Swarm.GetCounters().aliveEnemies, bp.bossChargePose);
        AutoTestLog(line);
    }
    // 石の守護獣の様子（0.25 秒毎）+ "roar" の試しの削り（2026-10-29）
    if (beast && t >= s_NextSandLog)
    {
        s_NextSandLog += 0.25f;
        const auto& bp = m_Swarm.GetBomberParams();
        const Vector3 sh = m_BossAttacks.BeastShadow();
        snprintf(line, sizeof(line), "bossmoves beast %s t %.2f dist %.2f phase %d hover %.2f fly %.0f wing %.2f spin %.2f pose %.2f petrify %.2f stone %.2f petrified %u hp %.2f roaring %d crush %d shadowDist %.2f shards %d crushHits %u shardHits %u",
            tr.name, t, flatDist(), m_BossAttacks.BeastPhase2() ? 2 : 1, bp.bossHover, bp.bossFly, bp.bossWing, bp.bossSpin, bp.bossChargePose,
            m_BossAttacks.BeastPetrify(), m_BossAttacks.BeastStoneLeft(), m_BeastPetrified, m_Stage.BossHpRatio(), m_BossAttacks.IsRoaring() ? 1 : 0,
            m_BossAttacks.BeastCrushPhase(), std::sqrt((tf.position.x - sh.x) * (tf.position.x - sh.x) + (tf.position.z - sh.z) * (tf.position.z - sh.z)),
            (int)m_BossAttacks.BeastShards().size(), m_BossAttacks.beastCrushHits, m_BossAttacks.beastShardHits);
        AutoTestLog(line);
        if (tr.chip && !m_BossAttacks.BeastPhase2())
        {
            Swarm::Area a;
            a.center = boss;
            a.radius = 3.0f;
            a.damage = 150.0f;
            a.tickInterval = 0.2f;
            a.timeLeft = 0.22f;
            m_Swarm.SpawnArea(a);
        }
    }
    // 石化しきった瞬間（2026-11-05）：体が灰色になり、アニメが止まっているか。寄って 0.5 秒後に 1 枚
    if (beast && m_Registry.Has<SkinnedAnimComponent>(m_Player))
    {
        static float s_StoneAt = -1.0f;   // -1 = まだ、-10 = 撮った
        const auto& a = m_Registry.Get<SkinnedAnimComponent>(m_Player);
        if (s_StoneAt == -1.0f && m_BossAttacks.BeastStoneLeft() > 0.0f)
        {
            s_StoneAt = t;
            auto& cam = m_Camera.Camera();
            cam.distance = 7.0f;
            cam.SetPitch(18.0f);
            cam.SnapToTarget();
            snprintf(line, sizeof(line), "bossmoves beast stone start hitFlash %.2f color %.2f,%.2f,%.2f animTime %.3f",
                a.hitFlash, a.hitFlashColor.x, a.hitFlashColor.y, a.hitFlashColor.z, a.base.time);
            AutoTestLog(line);
        }
        if (s_StoneAt >= 0.0f && t >= s_StoneAt + 0.5f)
        {
            snprintf(line, sizeof(line), "bossmoves beast stone later hitFlash %.2f animTime %.3f stoneLeft %.2f",
                a.hitFlash, a.base.time, m_BossAttacks.BeastStoneLeft());
            AutoTestLog(line);
            AutoTestLog("bossmoves look stone");
            s_StoneAt = -1.0e6f;   // 息の煙で体が隠れるので、寄った絵は全部の試しの後（m_AutoStep 13）で息なしに撮る
        }
    }
    // 砂嵐の魔神の様子（0.25 秒毎）+ "roar" の試しの削り
    if (djinn && t >= s_NextSandLog)
    {
        s_NextSandLog += 0.25f;
        const auto& bp = m_Swarm.GetBomberParams();
        int devilHit = 0;
        for (const DjinnDevil& d : m_BossAttacks.Devils()) devilHit += d.hitDone ? 1 : 0;
        snprintf(line, sizeof(line), "bossmoves djinn %s t %.2f dist %.2f phase %d hover %.2f fly %.0f keep %.0f-%.0f sink %.2f devils %d devilHit %d pose %.2f hp %.2f roaring %d",
            tr.name, t, flatDist(), m_BossAttacks.DjinnPhase2() ? 2 : 1, bp.bossHover, bp.bossFly, bp.bossKeepMin, bp.bossKeepMax, bp.bossBurrow,
            (int)m_BossAttacks.Devils().size(), devilHit, bp.bossChargePose, m_Stage.BossHpRatio(), m_BossAttacks.IsRoaring() ? 1 : 0);
        AutoTestLog(line);
        if (tr.chip && !m_BossAttacks.DjinnPhase2())
        {
            Swarm::Area a;
            a.center = boss;
            a.radius = 3.0f;
            a.damage = 150.0f;
            a.tickInterval = 0.2f;
            a.timeLeft = 0.22f;
            m_Swarm.SpawnArea(a);
        }
    }
    // ---- 避け方 ----
    pcs.testJump = false;
    if (tr.dodge == Dodge::Jump && !s_Jumped)
    {
        // 尾の薙ぎ払い（石の守護獣）：回り始める瞬間に跳ぶ（尾は体の後ろから回って来て、前のプレイヤーへは半周 = 回りの真ん中で届く）
        const float jumpAt = (tr.move == BossMove::TailSweep) ? m_BossAttacks.beast.sweepWindup
            : m_BossAttacks.waveWindup + (tr.dist - 2.0f) / (std::max)(m_BossAttacks.waveSpeed, 1.0f);
        if (t >= jumpAt) { pcs.testJump = true; s_Jumped = true; AutoTestLog("bossmoves jump"); }
    }
    // 天からの圧殺の「影から出る」：走って影から出た後、広がって来る衝撃波を跳ぶ（叩き潰しの 0.2 秒後）
    if (tr.move == BossMove::SkyCrush && tr.dodge == Dodge::Away && !s_Jumped)
    {
        const BeastTune& bt = m_BossAttacks.beast;
        if (t >= bt.crushRise + bt.crushTrack + bt.crushLock + bt.crushFall + 0.2f) { pcs.testJump = true; s_Jumped = true; AutoTestLog("bossmoves jump"); }
    }
    if (tr.dodge == Dodge::Side && tr.move == BossMove::ClawCombo)
    {
        // 爪の三連（10-29）：帯が出る度に、その帯と直角で線から離れる方へ走る。一方向へ走り続けると 3 回目には
        // 突っ込みの向きへ逃げる形になり、突っ込みの終わりで追い付かれる（人は帯毎に横へ避ける）
        for (const BossAttacks::Visual& v : m_BossAttacks.Visuals())
        {
            if (v.shape >= 0.0f) continue;
            const Vector3 along(std::sin(v.yaw), 0.0f, std::cos(v.yaw));
            Vector3 n(along.z, 0.0f, -along.x);
            if (n.Dot(tf.position - v.center) < 0.0f) n = -n;
            s_SideDir = n;
        }
        Vector3 camF = m_Camera.Camera().GetForward(); camF.y = 0.0f; camF.Normalize();
        Vector3 camR = m_Camera.Camera().GetRight();   camR.y = 0.0f; camR.Normalize();
        const bool running = t >= tr.dodgeAt && t < tr.dodgeAt + tr.dodgeLen;
        pcs.testMove = running ? Vector2(s_SideDir.Dot(camR), s_SideDir.Dot(camF)) : Vector2::Zero;
    }
    else if (tr.dodge == Dodge::Weave)
    {
        // 石羽の散弾（10-29）：1 回目が届く前に隙間へ 1 歩（0.24 秒 ≒ 1.3m = 10m 先の 7.5°）、通り過ぎたら 2 回目の前に元へ
        const float back = tr.dodgeAt + tr.dodgeLen;
        pcs.testMove = (t >= tr.dodgeAt && t < tr.dodgeAt + 0.24f) ? Vector2(1.0f, 0.0f)
            : (t >= back && t < back + 0.24f) ? Vector2(-1.0f, 0.0f) : Vector2::Zero;
    }
    else if (tr.dodge == Dodge::Side)
        pcs.testMove = (t >= tr.dodgeAt && t < tr.dodgeAt + tr.dodgeLen) ? Vector2(1.0f, 0.0f) : Vector2::Zero;   // カメラの右（= 突進の向きと直角）
    if (tr.dodge == Dodge::Gap && t >= tr.dodgeAt)
    {
        // 砂旋の輪の中心から、Boss から離れる向きに一番近い「隣り合う 2 本の真ん中」の向きへ走る（一度決めたら変えない）
        if (s_SideDir.LengthSquared() < 1e-3f && !m_BossAttacks.Devils().empty())
        {
            const auto& devs = m_BossAttacks.Devils();
            const Vector3 c = devs.front().center;
            Vector3 want = tf.position - boss;
            want.y = 0.0f;
            want.Normalize();
            float best = -2.0f;
            for (size_t i = 0; i < devs.size(); ++i)
            {
                const float a0 = devs[i].angle;
                float a1 = devs[(i + 1) % devs.size()].angle;
                while (a1 <= a0) a1 += 6.2831853f;
                const float mid = (devs.size() == 1) ? a0 + 3.1415927f : 0.5f * (a0 + a1);
                const Vector3 dir(std::cos(mid), 0.0f, std::sin(mid));
                if (dir.Dot(want) > best) { best = dir.Dot(want); s_SideDir = dir; }
            }
            snprintf(line, sizeof(line), "bossmoves %s gap dir %.2f,%.2f centre %.1f,%.1f", tr.name, s_SideDir.x, s_SideDir.z, c.x, c.z);
            AutoTestLog(line);
        }
        Vector3 camF = m_Camera.Camera().GetForward(); camF.y = 0.0f; camF.Normalize();
        Vector3 camR = m_Camera.Camera().GetRight();   camR.y = 0.0f; camR.Normalize();
        pcs.testMove = (t < tr.dodgeAt + tr.dodgeLen) ? Vector2(s_SideDir.Dot(camR), s_SideDir.Dot(camF)) : Vector2::Zero;
    }    if (tr.dodge == Dodge::Away)
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
