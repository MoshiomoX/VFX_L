// ============================================================
// AutoTestSchoolBench.cpp
// TEMP-TEST: VFXL_SPELL_LAB=1 + VFXL_BATTLE_AUTOTEST=schoolbench
// 流派ごとの DPS（2026-10-30、ユーザー「5 つの流派を 1 つずつ取り出し、強化した時の DPS と、
// 数値だけで計算した DPS を比べたい」。強化は測る為の組み合わせで、数値は変えない）。
// 流派 = 3 つの魔法の組（稀有度の分岐では全部単独で撃つ）:
//   火（ファイアボール + ストーンショット + メテオ）/ 雷・奥術（ホーミングボルト + アークボルト + 魔導光線）/
//   毒（泥沼 + 胞子 + 猛毒の沼）/ 氷（アイスランス + ストーンショット + アイススパイク）/ 風（渦 + 突風 + 竜巻）
// 流派ごとに 3 通り：base（魔法だけ）/ runes（+ ルーン 3 つ）/ tuned（+ ルーン 3 つ + 効果の枠 2 枚）。
//   ルーン・枠は流派に合う物を選んだ（BuildSchools の表）。ルーンは影響が届く魔法の数が一番多い所へ、
//   魔法は効果の枠に一番多く乗る所（同じなら真ん中に近い所）へ、総当たりで置く
// 場面は spellbench と同じ：single（正面に的 1 体、HP 100 万）/ crowd（半径 3m に 30 体、各 HP 10 万）/
//   fodder（30 体、各 HP 25、全滅までの秒）。2 秒撃たせてから 8 秒測る。風は突風が届かないので的を 6m に置く（他は 10m）
// 「数値の DPS」（theory）= 集約後の数値だけで、1 体に全部当たると見なした単体 DPS:
//   弾 = (弾の威力 × 当たる弾数 + 着弾の範囲の威力 × tick 数) × 弾数 × 連発数 / 発動間隔
//     （Drop / Lob / 線の魔法は弾自体は当たらない。線は 1 本につき範囲 1 つが当たる）
//   範囲 = 1 tick の威力 × tick 数 / 発動間隔（tick 数 = OneShot 1、他は 持続 / tick 間隔、最低 1。泥沼は飛沫の 1 回）
// 行：`schoolbench layout …`、`schoolbench theory <組> <魔法> dps <値> …`、`schoolbench <組> <場面> …`、
//   最後に `schoolbench table <流派> <組> single crowd ratio clear left theory eff mpps` を並べて `schoolbench done`
// 10-30 追加：`VFXL_SCHOOLS=arcane,wind` = その流派だけ / `VFXL_SCHOOL_VARIANTS=tuned` = その組だけ / `VFXL_BENCH_SPEED=<m/s>` = 的が歩いて来る（突風に飛ばされた的が
//   戻って来る。0 = 動かない）/ single と crowd の終わりに `schoolbench <組> <場面> dist avg min max`（生きている的とプレイヤーの水平距離）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Swarm/ProjectileProfile.h"

namespace
{
    class AutoTestSchoolBench final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Arrange(BackpackComponent& bp, const struct Trial& tr);
        float Theory(const WandComponent& wand, const char* name);
    };

    struct Trial
    {
        std::string school, variant;
        std::vector<ItemID> spells;    // 大きい物から置く
        std::vector<ItemID> runes;
        std::vector<ItemID> frames;    // 効果の枠
        float dist = 10.0f;
    };
    struct School { const char* name; std::vector<ItemID> spells, runes, frames; float dist; };

    constexpr float kWarm = 2.0f;
    constexpr float kMeasure = 8.0f;
    constexpr float kSingleHp = 1.0e6f;
    constexpr float kCrowdHp = 1.0e5f;
    constexpr int   kCrowd = 30;
    constexpr float kFodderHp = 25.0f;
    constexpr float kFodderCap = 20.0f;

    struct Result { float single = 0, crowd = 0, clear = 0, theory = 0, mpps = 0; int left = 0; };

    // 流派ごとのルーン・効果の枠（測る為に選んだ物）:
    //   二重詠唱 = 弾は 2 連発（間隔 ×1.25）、範囲は持続 ×1.5 / 加速 = 間隔 ×0.8（どの流派にも効く）
    //   一撃の枠 = 弾が当たった時の威力 ×1.25（単体の弾が主力の火・雷・氷）/ 範囲の枠 = 判定 ×1.2（範囲が主力の毒・風）
    //   3 つ目のルーン：火・毒・風・氷 = 拡大鏡（判定 ×1.2、弾の威力 ×1.1、線・光線が長い）/ 雷 = 連鎖（群れ向け）
    std::vector<School> BuildSchools()
    {
        using I = ItemID;
        return {
            { "fire",   { I::Meteor, I::Fireball, I::StoneShot },  { I::DoubleCastRune, I::HasteRune, I::Magnifier }, { I::FrameHaste, I::FrameFocus }, 10.0f },
            { "arcane", { I::Beam, I::ArcBolt, I::HomingBolt },    { I::DoubleCastRune, I::HasteRune, I::ChainRune }, { I::FrameHaste, I::FrameFocus }, 10.0f },
            { "poison", { I::Poison, I::Swamp, I::Spore },         { I::DoubleCastRune, I::HasteRune, I::Magnifier }, { I::FrameHaste, I::FrameArea }, 10.0f },
            { "ice",    { I::IceSpike, I::StoneShot, I::IceLance }, { I::DoubleCastRune, I::HasteRune, I::Magnifier }, { I::FrameHaste, I::FrameFocus }, 10.0f },
            { "wind",   { I::Twister, I::Gust, I::VoidVortex },    { I::DoubleCastRune, I::HasteRune, I::Magnifier }, { I::FrameHaste, I::FrameArea }, 6.0f },
        };
    }

    std::vector<Trial> BuildTrials()
    {
        char only[128] = {}, variants[64] = {};
        GetEnvironmentVariableA("VFXL_SCHOOLS", only, sizeof(only));
        GetEnvironmentVariableA("VFXL_SCHOOL_VARIANTS", variants, sizeof(variants));   // 例 tuned
        std::vector<Trial> t;
        for (const School& s : BuildSchools())
        {
            if (only[0] && !std::strstr(only, s.name)) continue;
            t.push_back({ s.name, "base", s.spells, {}, {}, s.dist });
            t.push_back({ s.name, "runes", s.spells, s.runes, {}, s.dist });
            t.push_back({ s.name, "tuned", s.spells, s.runes, s.frames, s.dist });
        }
        if (variants[0])
            t.erase(std::remove_if(t.begin(), t.end(), [&](const Trial& x) { return !std::strstr(variants, x.variant.c_str()); }), t.end());
        return t;
    }

    float TargetSpeed()
    {
        char v[32] = {};
        return (GetEnvironmentVariableA("VFXL_BENCH_SPEED", v, sizeof(v)) > 0) ? (float)std::atof(v) : 0.0f;
    }

    const char* ItemName(ItemID id)
    {
        const ItemCommon* ic = ItemDatabase::GetCommon(id);
        return ic ? ic->name : "?";
    }

    // 範囲 1 回が 1 体に与える合計（tick 数 × 威力）
    float AreaHitTotal(const AreaProfile& ap, float damagePerTick)
    {
        if (ap.kind == AreaProfile::Kind::OneShot || ap.splashOnly) return damagePerTick;
        const float ticks = (ap.tickInterval > 0.0f) ? (std::max)(1.0f, ap.duration / ap.tickInterval) : 1.0f;
        return damagePerTick * ticks;
    }
}

// ============================================================
// 置き方：9 枚の 3×3 枠のうち真ん中の 1 枚を外して効果の枠を入れる → 魔法 → ルーン
// ============================================================
void AutoTestSchoolBench::Arrange(BackpackComponent& bp, const Trial& tr)
{
    char line[320];
    ClearBackpackItems(bp);
    FillAllFrames(bp);
    if (!tr.frames.empty())
    {
        for (int i = 0; i < (int)bp.frames.size(); ++i)
            if (bp.frames[i].row == 3 && bp.frames[i].col == 3) { BackpackLogic::RemoveFrame(bp, i); break; }
        // 大きい枠から置く（L 字の 3 マスを先に置くと真ん中が埋まり、2×2 が入らない）
        std::vector<ItemID> frames = tr.frames;
        auto cellsOf = [](ItemID id) { const ItemCommon* ic = ItemDatabase::GetCommon(id); return ic ? (int)ic->occupyCells.size() : 0; };
        std::stable_sort(frames.begin(), frames.end(), [&](ItemID a, ItemID b) { return cellsOf(a) > cellsOf(b); });
        for (ItemID f : frames)
        {
            bool ok = false;
            for (int r = 3; r <= 5 && !ok; ++r)
                for (int c = 3; c <= 5 && !ok; ++c)
                    for (int rot = 0; rot < 4 && !ok; ++rot)
                        if (BackpackLogic::CanPlaceFrame(bp, f, r, c, rot))
                            ok = BackpackLogic::PlaceFrame(bp, f, r, c, rot) >= 0;
            if (!ok) { snprintf(line, sizeof(line), "schoolbench %s %s frame place failed %s", tr.school.c_str(), tr.variant.c_str(), ItemName(f)); AutoTestLog(line); }
        }
        // 真ん中の残りのマスは普通の小さい枠で埋める（埋まらないマスは空く）
        const ItemID fill[] = { ItemID::FrameL, ItemID::Frame2x3, ItemID::FrameLine4, ItemID::FrameCross };
        for (ItemID f : fill)
            for (int r = 1; r <= 5; ++r)
                for (int c = 1; c <= 5; ++c)
                    for (int rot = 0; rot < 4; ++rot)
                        if (BackpackLogic::CanPlaceFrame(bp, f, r, c, rot)) BackpackLogic::PlaceFrame(bp, f, r, c, rot);
    }

    // 魔法：効果の枠に乗る数 × 100 − 真ん中からの距離 が一番大きい所
    std::vector<int> spellIdx;
    for (ItemID id : tr.spells)
    {
        int bestR = -1, bestC = -1, bestRot = 0;
        float best = -1.0e9f;
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                for (int rot = 0; rot < 4; ++rot)
                {
                    if (!BackpackLogic::CanPlace(bp, id, r, c, rot)) continue;
                    const int idx = BackpackLogic::Place(bp, id, r, c, rot);
                    if (idx < 0) continue;
                    const float score = 100.0f * (float)BackpackLogic::GetEffectFrames(bp, idx).size()
                        - std::sqrt((float)((r - 4) * (r - 4) + (c - 4) * (c - 4)));
                    BackpackLogic::Remove(bp, idx);
                    if (score > best) { best = score; bestR = r; bestC = c; bestRot = rot; }
                }
        const int idx = (bestR >= 0) ? BackpackLogic::Place(bp, id, bestR, bestC, bestRot) : -1;
        if (idx < 0) { snprintf(line, sizeof(line), "schoolbench %s %s place failed %s", tr.school.c_str(), tr.variant.c_str(), ItemName(id)); AutoTestLog(line); }
        else spellIdx.push_back(idx);
    }

    // ルーン：影響が届く魔法の数が一番多い所（0 なら置かない）
    for (ItemID rune : tr.runes)
    {
        int bestR = -1, bestC = -1, bestRot = 0, best = 0;
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                for (int rot = 0; rot < 4; ++rot)
                {
                    if (!BackpackLogic::CanPlace(bp, rune, r, c, rot)) continue;
                    const int ri = BackpackLogic::Place(bp, rune, r, c, rot);
                    if (ri < 0) continue;
                    int reach = 0;
                    for (int si : spellIdx)
                    {
                        const std::vector<int> inf = BackpackLogic::GetInfluencers(bp, si);
                        if (std::find(inf.begin(), inf.end(), ri) != inf.end()) ++reach;
                    }
                    BackpackLogic::Remove(bp, ri);
                    if (reach > best) { best = reach; bestR = r; bestC = c; bestRot = rot; }
                }
        if (bestR < 0 || BackpackLogic::Place(bp, rune, bestR, bestC, bestRot) < 0)
        {
            snprintf(line, sizeof(line), "schoolbench %s %s rune place failed %s", tr.school.c_str(), tr.variant.c_str(), ItemName(rune));
            AutoTestLog(line);
        }
    }

    // 置いた結果（魔法ごとに届いているルーン・乗っている効果の枠）
    for (int si : spellIdx)
    {
        std::string s = std::string("schoolbench layout ") + tr.school + " " + tr.variant + " " + ItemName(bp.items[si].id) + " runes:";
        for (int ri : BackpackLogic::GetInfluencers(bp, si)) { s += " "; s += ItemName(bp.items[ri].id); }
        s += " frames:";
        for (int fi : BackpackLogic::GetEffectFrames(bp, si)) { s += " "; s += ItemName(bp.frames[fi].id); }
        AutoTestLog(s.c_str());
    }
    bp.dirty = true;
}

// 集約後の数値だけで計算した単体 DPS（1 体に全部当たると見なす）。魔法ごとに 1 行
float AutoTestSchoolBench::Theory(const WandComponent& wand, const char* name)
{
    char line[360];
    float total = 0.0f;
    for (const SpellStats& s : wand.spells)
    {
        const ProjectileProfile& pp = ProjectileProfileDB::At(s.profile);
        const bool noDirect = s.triggerLine || pp.mode == Swarm::MotionMode::Drop || pp.mode == Swarm::MotionMode::Lob;
        const float direct = noDirect ? 0.0f : s.damage;
        float area = 0.0f;
        if (!pp.hitArea.empty())
        {
            const int ai = AreaProfileDB::IndexOf(pp.hitArea);
            if (ai > 0)
            {
                const AreaProfile& ap = AreaProfileDB::At(ai);
                area = AreaHitTotal(ap, ap.damage * s.areaDamageMul);
            }
        }
        const float perCast = (direct + area) * (float)s.projectileCount * (float)s.castCount;
        const float dps = (s.castInterval > 0.0f) ? perCast / s.castInterval : 0.0f;
        total += dps;
        snprintf(line, sizeof(line), "schoolbench theory %s %s dps %.1f direct %.1f area %.1f count %d casts %d interval %.3f areaMul %.2f radius %.2f chain %d",
            name, ItemName(s.id), dps, direct, area, s.projectileCount, s.castCount, s.castInterval, s.areaDamageMul, s.radius, s.chainCount);
        AutoTestLog(line);
    }
    for (const AreaStats& a : wand.areas)
    {
        const AreaProfile& ap = AreaProfileDB::At(a.profile);
        float ticks = 1.0f;
        if (ap.kind != AreaProfile::Kind::OneShot && !ap.splashOnly && a.tickInterval > 0.0f)
            ticks = (std::max)(1.0f, a.duration / a.tickInterval);
        const float dps = (a.castInterval > 0.0f) ? a.damagePerTick * ticks / a.castInterval : 0.0f;
        total += dps;
        snprintf(line, sizeof(line), "schoolbench theory %s %s dps %.1f tick %.1f ticks %.1f interval %.3f radius %.2f length %.1f chain %d",
            name, ItemName(a.id), dps, a.damagePerTick, ticks, a.castInterval, a.radius, a.length, a.chainCount);
        AutoTestLog(line);
    }
    return total;
}

// ============================================================
// 手順は spellbench と同じ：消す → 置く（single の時だけ）→ 的 → kWarm 秒 → HP を読む → kMeasure 秒 → もう一度読む
// ============================================================
void AutoTestSchoolBench::Run()
{
    static std::vector<Trial> s_Trials;
    static std::vector<Result> s_Results;
    static int s_Trial = 0, s_Scene = 0, s_Phase = 0;
    static float s_PhaseT = 0.0f, s_NextPoll = 0.0f;
    static double s_Hp0 = 0.0;
    char line[360];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& wand = m_Registry.Get<WandComponent>(m_Player);

    auto sumHp = [&]() -> double
    {
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        if (!m_Swarm.DebugReadEnemies(en, st)) return -1.0;
        double sum = 0.0;
        for (size_t i = 0; i < en.size(); ++i)
            if (st[i] != Swarm::kStateDead) sum += Swarm::HpFromFixed(en[i].hp);
        return sum;
    };

    // 生きている的とプレイヤーの水平距離（平均 / 最小 / 最大）
    auto logDist = [&](const char* trialName, const char* scene)
    {
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        if (!m_Swarm.DebugReadEnemies(en, st)) return;
        float sum = 0.0f, mn = 1.0e9f, mx = 0.0f;
        int n = 0;
        for (size_t i = 0; i < en.size(); ++i)
        {
            if (st[i] == Swarm::kStateDead) continue;
            const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            sum += d; mn = (std::min)(mn, d); mx = (std::max)(mx, d); ++n;
        }
        snprintf(line, sizeof(line), "schoolbench %s %s dist avg %.2f min %.2f max %.2f alive %d",
            trialName, scene, n ? sum / n : 0.0f, n ? mn : 0.0f, mx, n);
        AutoTestLog(line);
    };

    if (m_AutoStep == 0)
    {
        if (m_AutoTime < 1.0f) return;
        s_Trials = BuildTrials();
        if (s_Trials.empty()) { AutoTestLog("schoolbench no trials (VFXL_SCHOOLS)"); AutoTestLog("schoolbench done"); m_AutoStep = 2; return; }
        s_Results.assign(s_Trials.size(), Result{});
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(35.0f);
        cam.distance = 14.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "schoolbench start trials %d targetSpeed %.2f", (int)s_Trials.size(), TargetSpeed());
        AutoTestLog(line);
        s_Trial = 0; s_Scene = 0; s_Phase = 0;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    const Trial& tr = s_Trials[s_Trial];
    const std::string fullName = tr.school + "/" + tr.variant;
    const char* name = fullName.c_str();

    auto nextScene = [&]()
    {
        s_Phase = 0;
        if (++s_Scene < 3) return;
        s_Scene = 0;
        if (++s_Trial < (int)s_Trials.size()) return;
        for (size_t i = 0; i < s_Trials.size(); ++i)
        {
            const Result& r = s_Results[i];
            snprintf(line, sizeof(line), "schoolbench table %s %s single %.1f crowd %.1f ratio %.2f clear %.2f left %d theory %.1f eff %.2f mpps %.1f dist %.0f",
                s_Trials[i].school.c_str(), s_Trials[i].variant.c_str(), r.single, r.crowd, r.single > 0.0f ? r.crowd / r.single : 0.0f,
                r.clear, r.left, r.theory, r.theory > 0.0f ? r.single / r.theory : 0.0f, r.mpps, s_Trials[i].dist);
            AutoTestLog(line);
        }
        m_Swarm.KillAll();
        wand.castingPaused = true;
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("schoolbench done");
        m_AutoStep = 2;
    };

    if (s_Phase == 0)
    {
        m_Swarm.KillAll();
        m_Swarm.ClearProjectiles();
        m_Swarm.ClearAreas();
        if (s_Scene == 0) Arrange(bp, tr);
        wand.castingPaused = false;
        const float gy = m_Swarm.GetAIParams().groundY;
        const Vector3 c = tf.position + Vector3(0.0f, 0.0f, tr.dist);
        const float speed = TargetSpeed();
        if (s_Scene == 0)
            m_Swarm.SpawnEnemy(Vector3(c.x, m_Grid.SampleHeight(c.x, c.z) + gy, c.z), kSingleHp, speed);
        else
            for (int i = 0; i < kCrowd; ++i)
            {
                const float rr = 3.0f * std::sqrt(((float)i + 0.5f) / (float)kCrowd);
                const float a = (float)i * 2.39996323f;
                const float x = c.x + std::cos(a) * rr, z = c.z + std::sin(a) * rr;
                m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), (s_Scene == 2) ? kFodderHp : kCrowdHp, speed);
            }
        s_PhaseT = m_AutoTime;
        s_NextPoll = 0.0f;
        s_Phase = (s_Scene == 2) ? 3 : 1;
        return;
    }

    if (s_Phase == 3)
    {
        if (m_AutoTime < s_NextPoll) return;
        s_NextPoll = m_AutoTime + 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        if (!m_Swarm.DebugReadEnemies(en, st)) return;
        int alive = 0;
        for (size_t i = 0; i < st.size(); ++i) if (st[i] != Swarm::kStateDead) ++alive;
        const float t = m_AutoTime - s_PhaseT;
        if ((alive == 0 && t > 0.5f) || t >= kFodderCap)
        {
            s_Results[s_Trial].clear = t;
            s_Results[s_Trial].left = alive;
            snprintf(line, sizeof(line), "schoolbench %s fodder clear %.2f s left %d", name, t, alive);
            AutoTestLog(line);
            nextScene();
        }
        return;
    }

    if (s_Phase == 1 && m_AutoTime >= s_PhaseT + kWarm)
    {
        s_Hp0 = sumHp();
        s_PhaseT = m_AutoTime;
        s_Phase = 2;
        if (s_Scene == 0)
        {
            float mpps = 0.0f;
            for (const SpellStats& s : wand.spells)
                if (s.castInterval > 0.0f) mpps += s.manaCost * (float)s.castCount / s.castInterval;
            for (const AreaStats& a : wand.areas)
                if (a.castInterval > 0.0f) mpps += a.manaCost / a.castInterval;
            s_Results[s_Trial].mpps = mpps;
            s_Results[s_Trial].theory = Theory(wand, name);
            if (tr.variant == "tuned") { snprintf(line, sizeof(line), "schoolbench look %s", tr.school.c_str()); AutoTestLog(line); }
        }
        return;
    }
    if (s_Phase == 2 && m_AutoTime >= s_PhaseT + kMeasure)
    {
        const double hp1 = sumHp();
        const float dps = (s_Hp0 >= 0.0 && hp1 >= 0.0) ? (float)((s_Hp0 - hp1) / (m_AutoTime - s_PhaseT)) : -1.0f;
        (s_Scene == 0 ? s_Results[s_Trial].single : s_Results[s_Trial].crowd) = dps;
        snprintf(line, sizeof(line), "schoolbench %s %s dps %.1f", name, s_Scene == 0 ? "single" : "crowd", dps);
        AutoTestLog(line);
        logDist(name, s_Scene == 0 ? "single" : "crowd");
        nextScene();
    }
}

REGISTER_BATTLE_AUTOTEST("schoolbench", AutoTestSchoolBench)
