// ============================================================
// AutoTestOutlet.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=outlet
// 出口魔法（2026-10-31）：出口の先の魔法が自分で撃たなくなり、弾が消えた所で発動するか。
//   正面 7〜11m に動かない的 12 体（HP 10 万）。全面に枠を敷き、段ごとに組み直して 6 秒撃つ。
//   段: spark   導きの火花 → メテオ
//       rotate  火花を裏返す → メテオは自分で撃つ魔法に戻る
//       chain   火花 → 分かれ玉 → ファイアボール + アイスランス（3 段）
//       seed    時限の種 → 渦（種が落ちてから渦が出るまでの秒を測る）
//       area    分かれ玉 → アークボルト + 突風 / 時限の種 → 竜巻（範囲魔法も出口の先に置ける）
//       loop    火花 2 つの出口を向かい合わせる（先に置いた方が上流、戻る出口は外れる = 片方だけ自分で撃つ）
//       free    比較：同じ火花 + メテオを繋がずに置く（どちらも自分で撃つ）
//   上流の弾を増やす（2026-10-31。VFXL_OUTLET_FROM=7 でここから。各 10 秒）:
//       fan      火花 + 分裂（下流なし）: 2 発目が別の敵へ飛ぶか（touched = 傷ついた敵の数）
//       m_base   火花 → メテオ
//       m_split  分裂を火花に / m_double 二重詠唱を火花に / m_two 火花 2 つ → メテオ 1 つ
//       m_down   比較：分裂をメテオに貼る
//       m_all    分裂 + 二重詠唱 + 火花 2 つ
//       f_base   火花 → ファイアボール（速い魔法）/ f_split 分裂を火花に
//   Noita から試しに入れた物（2026-11-01。VFXL_OUTLET_FROM=16 でここから）:
//       l_base   ファイアボールだけ / l_rune 分身のルーンを貼る
//       d_base   ファイアボール + 分裂 + 二重詠唱（一斉起爆なし）/ d_blast 一斉起爆を足す
//       d_larpa  ファイアボール + 分身 + 一斉起爆 / d_meteor 火花 2 つ（片方に分裂）→ メテオ + 一斉起爆
//       o_orbit  周回弾（半径 3.2m にも的を 6 体足す）/ o_mine 浮遊機雷 / o_field 機雷 + 分裂 + 一斉起爆 / o_chain 機雷 → ファイアボール
//   最後にカードとバックパック + 説明を撮る。
//   弾 1 発で必ず下流が 1 回出る = result の events と triggered が同じ数になる
//   行: `outlet <段> spell <名> triggered fromOutlet mask delay interval mana damage areaMul` / `outlet <段> result casts triggered lost touched`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestOutlet final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }
        bool SelfClock() const override { return true; }

    private:
        void Run(float dt);
        void BeginStage(int stage);
        void EndStage();
        float TotalHp();
        int TouchedTargets();
        void SpawnTargets();
        float StageTime() const;

        int   m_Stage = -1;
        float m_StageAt = 0.0f;
        uint32_t m_Casts0 = 0, m_Events0 = 0, m_Detonates0 = 0;
        bool  m_Look = false;
        bool  m_Started = false;
        float m_Hp0 = 0.0f;
        float m_SeedEventAt = -1.0f, m_SeedCastAt = -1.0f;
        float m_BlastAt = -1.0f;
        bool  m_BlastShot = false;
    };

    constexpr float kStageTime = 6.0f;
    constexpr float kShotStageTime = 10.0f;   // メテオのクールダウン（1.8 秒）を何度も回す
    const char* kStageNames[] = { "spark", "rotate", "chain", "seed", "area", "loop", "free",
        "fan", "m_base", "m_split", "m_double", "m_two", "m_down", "m_all", "f_base", "f_split",
        "l_base", "l_rune", "d_base", "d_blast", "d_larpa", "d_meteor",
        "o_orbit", "o_mine", "o_field", "o_chain" };
    constexpr int kFirstShotStage = 7;
    constexpr int kStages = 26;
}

float AutoTestOutlet::StageTime() const
{
    return (m_Stage >= kFirstShotStage) ? kShotStageTime : kStageTime;
}

void AutoTestOutlet::SpawnTargets()
{
    const float groundY = m_Swarm.GetAIParams().groundY;
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
    for (int k = 0; k < 12; ++k)
    {
        const float a = (-40.0f + 80.0f * (float)(k % 6) / 5.0f) * 0.0174533f;
        const float d = (k < 6) ? 7.0f : 11.0f;
        const float x = pp.x + std::sin(a) * d, z = pp.z + std::cos(a) * d;
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
    }
}

float AutoTestOutlet::TotalHp()
{
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    std::vector<Swarm::EnemyExtra> ex;
    float sum = 0.0f;
    if (m_Swarm.DebugReadEnemies(en, st, &ex))
        for (size_t i = 0; i < en.size() && i < st.size(); ++i)
            if (st[i] != Swarm::kStateDead) sum += (float)Swarm::HpFromFixed(en[i].hp);
    return sum;
}

int AutoTestOutlet::TouchedTargets()
{
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    std::vector<Swarm::EnemyExtra> ex;
    int n = 0;
    if (m_Swarm.DebugReadEnemies(en, st, &ex))
        for (size_t i = 0; i < en.size() && i < st.size(); ++i)
            if (st[i] != Swarm::kStateDead && (float)Swarm::HpFromFixed(en[i].hp) < 99999.5f) ++n;
    return n;
}

void AutoTestOutlet::BeginStage(int stage)
{
    m_Stage = stage;
    m_StageAt = m_AutoTime;
    m_Look = false;
    m_Swarm.KillAll();
    m_Swarm.ClearProjectiles();
    m_Swarm.ClearAreas();
    SpawnTargets();
    if (stage == 22)
    {
        const float groundY = m_Swarm.GetAIParams().groundY;
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        for (int k = 0; k < 6; ++k)
        {
            const float a = 1.0472f * (float)k;
            const float x = pp.x + std::sin(a) * 3.2f, z = pp.z + std::cos(a) * 3.2f;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
        }
    }

    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    ClearBackpackItems(bp);
    int a = -1, b = -1, c = -1;
    switch (stage)
    {
    case 0:   // 導きの火花 → メテオ
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 1, 0);
        b = PlaceAtOutlet(bp, a, ItemID::Meteor);
        break;
    case 1:   // 裏返す：出口は左へ向き、メテオから外れる
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 2, 2);
        b = BackpackLogic::Place(bp, ItemID::Meteor, 4, 5, 0);
        break;
    case 2:   // 3 段：火花 (4,0)(4,1) の出口 (4,2) = 分かれ玉の軸。左右の出口 (3,0) (3,4) が空く
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 0, 0);
        b = BackpackLogic::Place(bp, ItemID::OutletSplit, 3, 1, 0);
        c = PlaceAtOutlet(bp, b, ItemID::Fireball);
        PlaceAtOutlet(bp, b, ItemID::IceLance);
        break;
    case 3:   // 時限の種 → 渦
        a = BackpackLogic::Place(bp, ItemID::OutletSeed, 3, 2, 0);
        b = PlaceAtOutlet(bp, a, ItemID::VoidVortex);
        break;
    case 4:   // 範囲魔法
        a = BackpackLogic::Place(bp, ItemID::OutletSplit, 1, 2, 0);
        b = PlaceAtOutlet(bp, a, ItemID::ArcBolt);
        c = PlaceAtOutlet(bp, a, ItemID::Gust);
        {
            const int seed = BackpackLogic::Place(bp, ItemID::OutletSeed, 5, 1, 0);
            PlaceAtOutlet(bp, seed, ItemID::Twister);
        }
        break;
    case 5:   // 輪：向かい合わせ。右向きの火花の出口 (4,3) に左向きの火花が乗る
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 1, 0);
        b = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 4, 2);
        break;
    case 7:   // 火花 + 分裂、下流なし：分裂 (4,0)(4,1) の影響マス (4,2) = 火花
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 2, 0);
        b = BackpackLogic::Place(bp, ItemID::SplitRune, 4, 0, 0);
        break;
    case 8:   // 火花 (4,2)(4,3) の出口 (4,4) = メテオ（中心 (4,5)）の左腕
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 2, 0);
        if (stage == 11 || stage == 13)
            BackpackLogic::Place(bp, ItemID::OutletSpark, 3, 3, 0);      // 2 本目：出口 (3,5) = メテオの上の腕
        b = BackpackLogic::Place(bp, ItemID::Meteor, 4, 5, 0);
        if (stage == 9 || stage == 13)
            c = BackpackLogic::Place(bp, ItemID::SplitRune, 4, 0, 0);    // 影響マス (4,2) = 火花
        if (stage == 10 || stage == 13)
            BackpackLogic::Place(bp, ItemID::DoubleCastRune, 5, 2, 0);   // 影響マス (4,2)(4,3) = 火花
        if (stage == 12)
            c = BackpackLogic::Place(bp, ItemID::SplitRune, 4, 7, 0);    // 影響マス (4,6) = メテオの右腕
        break;
    case 14:  // 火花 → ファイアボール
    case 15:
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 2, 0);
        b = BackpackLogic::Place(bp, ItemID::Fireball, 4, 4, 0);
        if (stage == 15)
            c = BackpackLogic::Place(bp, ItemID::SplitRune, 4, 0, 0);
        break;
    case 16:  // ファイアボール (4,2)〜 + 分身のルーン (4,0)(4,1)：影響マス (4,2)
    case 17:
        a = BackpackLogic::Place(bp, ItemID::Fireball, 4, 2, 0);
        if (stage == 17)
            b = BackpackLogic::Place(bp, ItemID::LarpaRune, 4, 0, 0);
        break;
    case 18:  // 弾を空中に増やす：ファイアボール (4,2)〜 + 分裂 (4,0) + 二重詠唱 (6,2)：影響マス (5,2)(5,3)
    case 19:
        a = BackpackLogic::Place(bp, ItemID::Fireball, 4, 2, 0);
        b = BackpackLogic::Place(bp, ItemID::SplitRune, 4, 0, 0);
        BackpackLogic::Place(bp, ItemID::DoubleCastRune, 6, 2, 0);
        if (stage == 19)
            c = BackpackLogic::Place(bp, ItemID::Detonate, 7, 5, 0);
        break;
    case 20:  // 分身の弾もまとめて起爆する
        a = BackpackLogic::Place(bp, ItemID::Fireball, 4, 2, 0);
        b = BackpackLogic::Place(bp, ItemID::LarpaRune, 4, 0, 0);
        c = BackpackLogic::Place(bp, ItemID::Detonate, 7, 5, 0);
        break;
    case 21:  // 起爆した所でメテオが出る：火花 2 つ（片方に分裂）→ メテオ
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 2, 0);
        BackpackLogic::Place(bp, ItemID::OutletSpark, 3, 3, 0);
        b = BackpackLogic::Place(bp, ItemID::Meteor, 4, 5, 0);
        BackpackLogic::Place(bp, ItemID::SplitRune, 4, 0, 0);
        c = BackpackLogic::Place(bp, ItemID::Detonate, 7, 5, 0);
        break;
    case 22:  // 周回弾
        a = BackpackLogic::Place(bp, ItemID::OrbitGuard, 4, 2, 0);
        break;
    case 23:  // 浮遊機雷 (4,2)〜(5,3)
    case 24:  // + 分裂 (4,0) + 一斉起爆：飛んでいる機雷をまとめて起爆する
    case 25:  // 出口 (4,4) にファイアボール
        a = BackpackLogic::Place(bp, ItemID::OutletMine, 4, 2, 0);
        if (stage == 24)
        {
            b = BackpackLogic::Place(bp, ItemID::SplitRune, 4, 0, 0);
            BackpackLogic::Place(bp, ItemID::DoubleCastRune, 6, 2, 0);   // 影響マス (5,2)(5,3)：1 回に 4 個飛ぶ
            c = BackpackLogic::Place(bp, ItemID::Detonate, 7, 5, 0);
        }
        if (stage == 25)
            b = BackpackLogic::Place(bp, ItemID::Fireball, 4, 4, 0);
        break;
    default:  // 比較：繋がない
        a = BackpackLogic::Place(bp, ItemID::OutletSpark, 1, 1, 0);
        b = BackpackLogic::Place(bp, ItemID::Meteor, 6, 6, 0);
        break;
    }
    bp.dirty = true;
    m_Registry.Get<WandComponent>(m_Player).castingPaused = true;   // 的の HP を測ってから撃つ
    m_Started = false;
    m_Casts0 = m_WeaponSystem.GetTriggeredCasts();
    m_Events0 = m_WeaponSystem.GetTriggerEventsSeen();
    m_Detonates0 = m_WeaponSystem.GetDetonates();
    m_BlastAt = -1.0f;
    m_BlastShot = false;
    m_SeedEventAt = m_SeedCastAt = -1.0f;
    char line[160];
    snprintf(line, sizeof(line), "outlet %s begin t %.1f items %d a %d b %d c %d", kStageNames[stage], m_AutoTime, (int)bp.items.size(), a, b, c);
    AutoTestLog(line);
}

void AutoTestOutlet::EndStage()
{
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    char line[200];
    for (const auto& s : wand.spells)
    {
        snprintf(line, sizeof(line), "outlet %s spell %s triggered %d fromOutlet %d mask %08X delay %.1f interval %.2f mana %.1f count %d casts %d damage %.2f areaMul %.2f",
            kStageNames[m_Stage], ItemDatabase::GetCommon(s.id)->name, s.triggered ? 1 : 0, s.fromOutlet ? 1 : 0, s.triggerMask, s.triggerDelay,
            s.castInterval, s.manaCost, s.projectileCount, s.castCount, s.damage, s.areaDamageMul);
        AutoTestLog(line);
    }
    for (const auto& a : wand.areas)
    {
        snprintf(line, sizeof(line), "outlet %s area %s triggered %d fromOutlet %d delay %.1f",
            kStageNames[m_Stage], ItemDatabase::GetCommon(a.id)->name, a.triggered ? 1 : 0, a.fromOutlet ? 1 : 0, a.triggerDelay);
        AutoTestLog(line);
    }
    snprintf(line, sizeof(line), "outlet %s result items %d events %u triggered %u detonates %u lost %.0f dps %.0f touched %d seedWait %.2f",
        kStageNames[m_Stage], (int)bp.items.size(), m_WeaponSystem.GetTriggerEventsSeen() - m_Events0,
        m_WeaponSystem.GetTriggeredCasts() - m_Casts0, m_WeaponSystem.GetDetonates() - m_Detonates0,
        m_Hp0 - TotalHp(), (m_Hp0 - TotalHp()) / (StageTime() - 0.5f), TouchedTargets(),
        (m_SeedEventAt >= 0.0f && m_SeedCastAt >= 0.0f) ? m_SeedCastAt - m_SeedEventAt : -1.0f);
    AutoTestLog(line);
}

void AutoTestOutlet::Run(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<BackpackComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    if (lv.pendingChoices.empty()) lv.experience = 0.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
        FillAllFrames(bp);
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(42.0f);
        cam.distance = 18.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        char fromEnv[16] = {};
        const bool hasFrom = GetEnvironmentVariableA("VFXL_OUTLET_FROM", fromEnv, sizeof(fromEnv)) > 0;
        BeginStage(hasFrom ? std::clamp(atoi(fromEnv), 0, kStages - 1) : 0);
        m_AutoStep = 1;
        return;
    }

    if (m_AutoStep == 1)
    {
        const float t = m_AutoTime - m_StageAt;
        if (!m_Started && t >= 0.5f)
        {
            m_Hp0 = TotalHp();
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
            m_Started = true;
        }
        // 時限の種：最初の弾が消えた時刻と、最初に渦が撃たれた時刻
        if (m_Stage == 3)
        {
            if (m_SeedEventAt < 0.0f && m_WeaponSystem.GetTriggerEventsSeen() != m_Events0) m_SeedEventAt = m_AutoTime;
            if (m_SeedCastAt < 0.0f && m_WeaponSystem.GetTriggeredCasts() != m_Casts0) m_SeedCastAt = m_AutoTime;
        }
        // 一斉起爆の直後を撮る
        if (m_BlastAt < 0.0f && m_WeaponSystem.GetDetonates() != m_Detonates0) m_BlastAt = m_AutoTime;
        if (m_BlastAt >= 0.0f && !m_BlastShot && m_AutoTime - m_BlastAt >= 0.14f)
        {
            m_BlastShot = true;
            char line[64];
            snprintf(line, sizeof(line), "outlet look %s_blast", kStageNames[m_Stage]);
            AutoTestLog(line);
        }
        if (!m_Look && t >= 3.2f)
        {
            m_Look = true;
            char line[64];
            snprintf(line, sizeof(line), "outlet look %s", kStageNames[m_Stage]);
            AutoTestLog(line);
        }
        if (t >= StageTime())
        {
            EndStage();
            if (m_Stage + 1 < kStages) BeginStage(m_Stage + 1);
            else
            {
                // カード
                lv.ClearChoices();
                lv.pendingChoices = { ItemID::LarpaRune, ItemID::Detonate, ItemID::OutletMine, ItemID::OrbitGuard };
                m_StageAt = m_AutoTime;
                m_AutoStep = 2;
            }
        }
        return;
    }
    if (m_AutoStep == 2 && m_AutoTime - m_StageAt >= 1.0f)
    {
        AutoTestLog("outlet look cards");
        m_StageAt = m_AutoTime;
        m_AutoStep = 7;
        return;
    }
    if (m_AutoStep == 7 && m_AutoTime - m_StageAt >= 0.2f)
    {
        lv.ClearChoices();
        // バックパック：3 段の組 + 時限の種
        ClearBackpackItems(bp);
        BackpackLogic::Place(bp, ItemID::OutletSpark, 2, 0, 0);
        BackpackLogic::Place(bp, ItemID::DoubleCastRune, 3, 0, 0);   // 説明カードの x2 と間隔・MP を見る
        const int split = BackpackLogic::Place(bp, ItemID::OutletSplit, 1, 1, 0);
        PlaceAtOutlet(bp, split, ItemID::Fireball);
        PlaceAtOutlet(bp, split, ItemID::IceLance);
        const int seed = BackpackLogic::Place(bp, ItemID::OutletSeed, 6, 1, 0);
        PlaceAtOutlet(bp, seed, ItemID::Meteor);
        bp.dirty = true;
        m_GameUI.TestShow(1);
        m_StageAt = m_AutoTime;
        m_AutoStep = 3;
        return;
    }
    if (m_AutoStep == 3 && m_AutoTime - m_StageAt >= 0.8f)
    {
        AutoTestLog("outlet look bag");
        m_StageAt = m_AutoTime;
        m_AutoStep = 8;
        return;
    }
    if (m_AutoStep == 8 && m_AutoTime - m_StageAt >= 0.2f)
    {
        int idx = -1;
        for (int i = 0; i < (int)bp.items.size(); ++i)
            if (bp.items[i].id == ItemID::OutletSpark) idx = i;
        if (idx >= 0)
        {
            const ItemInfo::Sheet sheet = ItemInfo::DescribePlaced(bp, idx);
            std::string line = "outlet sheet traits";
            line += " " + std::to_string(sheet.traits.size());
            AutoTestLog(line.c_str());
            m_GameUI.TestTooltip(idx, { 820.0f, 200.0f });
        }
        m_StageAt = m_AutoTime;
        m_AutoStep = 4;
        return;
    }
    if (m_AutoStep == 4 && m_AutoTime - m_StageAt >= 0.8f)
    {
        AutoTestLog("outlet look tooltip");
        m_StageAt = m_AutoTime;
        m_AutoStep = 5;
        return;
    }
    if (m_AutoStep == 5 && m_AutoTime - m_StageAt >= 0.3f)
    {
        m_GameUI.TestTooltip(-1, { 0.0f, 0.0f });
        AutoTestLog("outlet done");
        m_AutoStep = 6;
    }
}

REGISTER_BATTLE_AUTOTEST("outlet", AutoTestOutlet)
